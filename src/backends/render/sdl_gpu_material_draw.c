#include <stddef.h>
#include <string.h>

#include "backends/render/sdl_gpu_filter.h"
#include "backends/render/sdl_gpu_material_draw.h"
#include "backends/render/sdl_gpu_powervr_order.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/shader_uniforms.h"
#include "engine/render/matrix.h"
#include "engine/render/scene_material_policy.h"

struct material_pass {
	struct mh_gpu_renderer	*gpu;
	SDL_GPUCommandBuffer	*command;
	SDL_GPURenderPass	*pass;
	const float		*matrix, *view, *models;
	float			*camera;
	float			material[MH_GPU_MATERIAL_FLOATS];
	int	reflections, direct3d, unavailable, skid, shadow, untextured;
};

static void
bind_model(const struct material_pass *state,
	const struct mh_gpu_primitive *item, int glenz)
{
	const struct mh_gpu_renderer *gpu;
	const struct mh_gpu_mesh *model;
	SDL_GPUBufferBinding vertex_binding, glenz_binding;
	const float *transform;
	float *camera_uniform;

	gpu = state->gpu;
	model = &gpu->meshes[item->model];
	camera_uniform = state->camera;
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	memset(&glenz_binding, 0, sizeof(glenz_binding));

	if (glenz) {
		glenz_binding.buffer = item->prepared ? gpu->prepared_buffer
						: model->glenz_vertices;
		glenz_binding.offset = item->prepared
			? (Uint32)(gpu->prepared_count *
				sizeof(struct mh_vertex))
			: 0;

		SDL_BindGPUVertexBuffers(state->pass, 1, &glenz_binding, 1);
	}

	transform = state->models == NULL ? mh_render_identity
					: state->models + item->model * 16;
	mh_render_matrix_multiply(camera_uniform, state->matrix, transform);
	mh_render_matrix_multiply(
		camera_uniform + MH_GPU_CAMERA_VIEW, state->view, transform);
	memcpy(camera_uniform + MH_GPU_CAMERA_MODEL, transform,
		16 * sizeof(float));

	if (item->prepared)
		memcpy(camera_uniform, mh_render_identity, 16 * sizeof(float));

	SDL_PushGPUVertexUniformData(state->command, 0, camera_uniform,
		MH_GPU_CAMERA_FLOATS * sizeof(float));
	vertex_binding.buffer =
		item->prepared ? gpu->prepared_buffer : model->vertices;
	SDL_BindGPUVertexBuffers(state->pass, 0, &vertex_binding, 1);
}

static size_t
prepare_material(struct material_pass *state,
	const struct mh_gpu_primitive *item, size_t list, int glenz,
	enum mh_gpu_pipeline *pipeline)
{
	const struct mh_gpu_mesh *model;
	const struct mh_draw *draw;
	struct mh_material_request request;
	struct mh_material_policy policy;
	enum mh_render_profile profile;

	model = &state->gpu->meshes[item->model];
	draw = &model->mesh->draws[item->draw];
	profile = state->gpu->powervr ? MH_RENDER_POWERVR
		: state->direct3d     ? MH_RENDER_DIRECT3D
				: MH_RENDER_GLIDE;
	request.list = list;
	request.texture = draw->texture;
	request.environment_texture = model->mesh->environment_texture;
	request.glenz_phong = model->glenz_phong;
	request.glenz_reflection = model->glenz_reflection;
	request.glenz = glenz;
	request.untextured = state->untextured;
	request.shadow = state->shadow;
	request.skid = state->skid;
	request.unavailable = state->unavailable;
	request.reflections = state->reflections;
	request.lit = model->lit;
	request.prelit = draw->prelit;
	request.fog = state->gpu->fog_enabled;
	request.alpha_test = draw->alpha_test;
	request.prepared = item->prepared;
	request.environment = draw->environment;

	mh_scene_material_policy(profile, &request, &policy);
	mh_gpu_material_uniforms(&policy, state->material);
	*pipeline = policy.pipeline;

	return policy.texture;
}

static int
draw_list(struct material_pass *state, size_t list, size_t head)
{
	struct mh_gpu_renderer *gpu;
	SDL_GPUGraphicsPipeline *pipeline, *bound_pipeline;
	enum mh_gpu_pipeline material_pipeline;
	SDL_GPUTextureSamplerBinding binding;
	const struct mh_gpu_primitive *item;
	const struct mh_gpu_mesh *model;
	const struct mh_draw *draw;
	size_t index, previous_model, texture_index;
	int phong, point_alpha, glenz;
	const float *lighting, *previous_lighting;
	float surface[4];

	gpu = state->gpu;
	phong = list == MH_LIST_GLENZ_PHONG;
	point_alpha = !state->untextured &&
		(list >= MH_LIST_ALPHA ||
			(!state->direct3d && list == MH_LIST_DISTANT_ALPHA));
	bound_pipeline = NULL;
	previous_model = MH_NO_PRIMITIVE;
	previous_lighting = NULL;

	for (index = head; index != MH_NO_PRIMITIVE; index = item->next) {

		item = &gpu->primitives[index];
		model = &gpu->meshes[item->model];
		draw = &model->mesh->draws[item->draw];
		glenz = model->glenz && (list == MH_LIST_ENVIRONMENT || phong);

		/* Ordinary environment faces retain Glide's clamped sampler. */
		binding.sampler = mh_gpu_material_sampler(gpu, state->direct3d,
			glenz, draw->environment, point_alpha);

		/* Return before the Phong branch. */
		if (phong && (!glenz || item->near_clipped))
			continue;

		texture_index = prepare_material(
			state, item, list, glenz, &material_pipeline);
		pipeline = mh_gpu_pipeline(
			gpu, state->direct3d, material_pipeline);
		lighting = MH_RENDER_BUILD_OPENMH && !state->untextured &&
				!state->shadow && !item->prepared
			? mh_openmh_light_buffer_surface(
				&model->openmh, item->first, surface)
			: NULL;

		if (lighting != NULL) {
			pipeline = gpu->openmh_pipelines.items[list <
						MH_LIST_DISTANT_COUNT
					? MH_GPU_PIPELINE_DISTANT
					: MH_GPU_PIPELINE_OPAQUE];

			if (lighting != previous_lighting)
				SDL_PushGPUVertexUniformData(state->command, 2,
					lighting,
					MH_OPENMH_LIGHT_FLOATS * sizeof(float));

			SDL_PushGPUVertexUniformData(
				state->command, 3, surface, sizeof(surface));
		}

		previous_lighting = lighting;

		if (pipeline != bound_pipeline) {
			SDL_BindGPUGraphicsPipeline(state->pass, pipeline);

			bound_pipeline = pipeline;
		}

		if (previous_model != item->model) {
			bind_model(state, item, glenz);
			previous_model = item->model;
		}

		SDL_PushGPUVertexUniformData(state->command, 1, state->material,
			sizeof(state->material));
		binding.texture =
			gpu->images[model->images[texture_index]].texture;
		SDL_BindGPUFragmentSamplers(state->pass, 0, &binding, 1);

		if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
			pipeline ==
				mh_gpu_pipeline(gpu, state->direct3d,
					MH_GPU_PIPELINE_ALPHA)) {
			if (!mh_gpu_powervr_order_defer(gpu, item->model,
				item->first, item->count, item->prepared,
				&binding, state->camera, state->material))
				return 0;

			continue;
		}

		SDL_DrawGPUPrimitives(state->pass, (Uint32)item->count, 1,
			(Uint32)item->first, 0);
	}

	return 1;
}

#if MH_RENDER_BUILD_OPENMH
static int
prepare_lighting(struct mh_gpu_renderer *gpu, const float *models)
{
	struct mh_openmh_light_buffer *lighting;
	struct mh_error error;
	const float *model, *root;
	size_t i;

	for (i = 0; i < gpu->mesh_count; i++) {
		lighting = &gpu->meshes[i].openmh;

		if (!lighting->enabled || gpu->meshes[i].hidden)
			continue;

		model = models ? models + i * 16 : mh_render_identity;
		root = models ? models + lighting->root * 16
			: mh_render_identity;

		if (mh_openmh_light_buffer_prepare(lighting,
			gpu->meshes[i].mesh, model, root,
			&error) != MH_ERROR_NONE)
			return SDL_SetError("%s", error.message);
	}

	return 1;
}
#endif

int
mh_gpu_draw_materials(struct mh_gpu_renderer *gpu,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass,
	const float matrix[16], const float view[16], const float *models,
	int reflections, enum mh_scene_effect effect, int direct3d,
	float camera_uniform[MH_GPU_CAMERA_FLOATS])
{
	struct material_pass state;
	size_t begin, end, list, head;

#if MH_RENDER_BUILD_OPENMH
	if (effect == MH_SCENE_EFFECT_OPAQUE && !prepare_lighting(gpu, models))
		return 0;
#endif

	state.gpu = gpu;
	state.command = command;
	state.pass = pass;
	state.matrix = matrix;
	state.view = view;
	state.models = models;
	state.camera = camera_uniform;
	state.reflections = reflections;
	state.direct3d = direct3d;
	state.unavailable = effect == MH_SCENE_EFFECT_UNAVAILABLE;
	state.skid = effect == MH_SCENE_EFFECT_SKIDS;
	state.shadow = effect == MH_SCENE_EFFECT_SHADOWS;
	state.untextured = state.unavailable || state.skid;
	memset(state.material, 0, sizeof(state.material));

	begin = state.shadow	    ? MH_LIST_SHADOW
		: state.skid	    ? MH_LIST_SKID
		: state.unavailable ? MH_LIST_UNAVAILABLE
				: 0;
	end = state.untextured || state.shadow ? begin + 1
					: MH_LIST_UNAVAILABLE;

	for (list = begin; list < end; list++) {
		head = gpu->list_heads[list == MH_LIST_GLENZ_PHONG
				? MH_LIST_ENVIRONMENT
				: list];

		if (head != MH_NO_PRIMITIVE && !draw_list(&state, list, head))
			return 0;
	}

	return 1;
}
