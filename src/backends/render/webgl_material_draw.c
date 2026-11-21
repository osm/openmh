#include <string.h>

#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_powervr_order.h"
#include "engine/render/matrix.h"
#include "engine/render/scene_material_policy.h"

struct material_pass {
	struct mh_webgl_drawing		*drawing;
	const struct mh_webgl_scene	*scene;
	const struct mh_webgl_materials	*materials;
	const struct mh_webgl_frame	*frame;
	int				unavailable, skid, shadow, untextured;
};

static void
bind_model(const struct material_pass *state,
	const struct mh_webgl_primitive *item, struct mh_webgl_draw_call *call)
{
	const float *transform;

	transform = state->frame->models == NULL
		? mh_render_identity
		: state->frame->models + item->model * 16;
	mh_render_matrix_multiply(
		call->camera, state->frame->matrix, transform);
	mh_render_matrix_multiply(call->camera + MH_GPU_CAMERA_VIEW,
		state->frame->view, transform);
	memcpy(call->camera + MH_GPU_CAMERA_MODEL, transform,
		16 * sizeof(float));

	if (item->prepared)
		memcpy(call->camera, mh_render_identity, 16 * sizeof(float));

	call->geometry = item->prepared
		? &state->materials->geometry
		: &state->scene->meshes[item->model].geometry;
	call->first = item->first;
	call->count = item->count;
}

static size_t
material_parameters(const struct material_pass *state,
	const struct mh_webgl_primitive *item, size_t list, int glenz,
	struct mh_webgl_draw_call *call)
{
	const struct mh_webgl_mesh *mesh;
	const struct mh_draw *draw;
	struct mh_material_request request;
	struct mh_material_policy policy;
	enum mh_render_profile profile;

	mesh = &state->scene->meshes[item->model];
	draw = &mesh->source->draws[item->draw];
	profile = state->scene->images.powervr ? MH_RENDER_POWERVR
		: state->frame->direct3d       ? MH_RENDER_DIRECT3D
					: MH_RENDER_GLIDE;
	request.list = list;
	request.texture = draw->texture;
	request.environment_texture = mesh->source->environment_texture;
	request.glenz_phong = mesh->glenz_phong;
	request.glenz_reflection = mesh->glenz_reflection;
	request.glenz = glenz;
	request.untextured = state->untextured;
	request.shadow = state->shadow;
	request.skid = state->skid;
	request.unavailable = state->unavailable;
	request.reflections = state->frame->reflections;
	request.lit = mesh->lit;
	request.prelit = draw->prelit;
	request.fog = state->frame->fog != NULL;
	request.alpha_test = draw->alpha_test;
	request.prepared = item->prepared;
	request.environment = draw->environment;

	mh_scene_material_policy(profile, &request, &policy);
	mh_gpu_material_uniforms(&policy, call->material);
	call->pipeline = policy.pipeline;

	return policy.texture;
}

static enum mh_webgl_program_kind
material_program(const struct material_pass *state, int glenz)
{
	if (state->scene->images.powervr)
		return MH_WEBGL_POWERVR;

	if (state->frame->direct3d)
		return glenz ? MH_WEBGL_DIRECT3D_GLENZ : MH_WEBGL_DIRECT3D;

	return glenz ? MH_WEBGL_MATERIAL_GLENZ : MH_WEBGL_MATERIAL;
}

static enum mh_error_code
prepare_material_call(const struct material_pass *state,
	const struct mh_webgl_primitive *item, size_t list, int glenz,
	int alpha, struct mh_webgl_draw_call *call, struct mh_error *error_out)
{
	const struct mh_webgl_mesh *mesh;
	const struct mh_draw *draw;
	size_t texture;

	mesh = &state->scene->meshes[item->model];
	draw = &mesh->source->draws[item->draw];

	call->program = material_program(state, glenz);

	call->sampler = mh_webgl_material_sampler(state->drawing,
		state->scene->images.powervr, state->frame->direct3d, glenz,
		draw->environment, alpha);
	bind_model(state, item, call);
	texture = material_parameters(state, item, list, glenz, call);
	call->lighting = MH_RENDER_BUILD_OPENMH && !state->untextured &&
			!state->shadow && !item->prepared
		? mh_openmh_light_buffer_surface(
			&mesh->openmh, item->first, call->surface)
		: NULL;

	if (call->lighting != NULL)
		call->program = MH_WEBGL_OPENMH;

	if (texture >= mesh->source->texture_count ||
		mesh->images[texture] >= state->scene->images.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL material texture");

	call->texture = state->scene->images.items[mesh->images[texture]]
				.texture.handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
submit_material_call(const struct material_pass *state,
	const struct mh_webgl_primitive *item,
	const struct mh_webgl_draw_call *call, struct mh_error *error_out)
{
	if (MH_RENDER_BUILD_POWERVR && state->scene->images.powervr &&
		call->pipeline == MH_GPU_PIPELINE_ALPHA)
		return mh_webgl_order_defer(state->drawing->order, call,
			&state->scene->meshes[item->model], state->materials,
			item->prepared, error_out);

	return mh_webgl_draw_submit(state->drawing, call, error_out);
}

static enum mh_error_code
draw_list(const struct material_pass *state, size_t list,
	struct mh_error *error_out)
{
	const struct mh_webgl_primitive *item;
	const struct mh_webgl_mesh *mesh;
	struct mh_webgl_draw_call call;
	enum mh_error_code status;
	size_t index;
	int glenz, alpha;

	memset(&call, 0, sizeof(call));
	mh_webgl_frame_camera(
		state->frame, state->scene->images.powervr, call.camera);
	alpha = !state->untextured &&
		(list >= MH_LIST_ALPHA ||
			(!state->frame->direct3d &&
				list == MH_LIST_DISTANT_ALPHA));
	index = state->materials->heads[list == MH_LIST_GLENZ_PHONG
			? MH_LIST_ENVIRONMENT
			: list];

	for (; index != MH_NO_PRIMITIVE; index = item->next) {
		item = &state->materials->items[index];
		mesh = &state->scene->meshes[item->model];
		glenz = mesh->glenz &&
			(list == MH_LIST_ENVIRONMENT ||
				list == MH_LIST_GLENZ_PHONG);

		/* Return before Phong on near clipping. */
		if (list == MH_LIST_GLENZ_PHONG &&
			(!glenz || item->near_clipped))
			continue;

		status = prepare_material_call(
			state, item, list, glenz, alpha, &call, error_out);

		if (status == MH_ERROR_NONE)
			status = submit_material_call(
				state, item, &call, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_draw_materials(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials,
	const struct mh_webgl_frame *frame, enum mh_scene_effect effect,
	struct mh_error *error_out)
{
	struct material_pass state;
	enum mh_error_code status;
	size_t begin, end, list;

	state.drawing = drawing;
	state.scene = scene;
	state.materials = materials;
	state.frame = frame;
	state.unavailable = effect == MH_SCENE_EFFECT_UNAVAILABLE;
	state.skid = effect == MH_SCENE_EFFECT_SKIDS;
	state.shadow = effect == MH_SCENE_EFFECT_SHADOWS;
	state.untextured = state.unavailable || state.skid;
	begin = state.shadow	    ? MH_LIST_SHADOW
		: state.skid	    ? MH_LIST_SKID
		: state.unavailable ? MH_LIST_UNAVAILABLE
				: 0;
	end = state.untextured || state.shadow ? begin + 1
					: MH_LIST_UNAVAILABLE;

	for (list = begin; list < end; list++) {
		status = draw_list(&state, list, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
