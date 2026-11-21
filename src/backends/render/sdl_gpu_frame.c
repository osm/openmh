#include <stddef.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_material_draw.h"
#include "backends/render/sdl_gpu_material_queue.h"
#include "backends/render/sdl_gpu_powervr_order.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_retro.h"
#include "backends/render/sdl_gpu_scene.h"
#include "backends/render/sdl_gpu_tron.h"
#include "backends/render/sdl_gpu_ui.h"
#include "backends/render/shader_uniforms.h"
#include "engine/render/matrix.h"
#include "engine/render/scene_effect_policy.h"

#define COLOR_NORMALIZER 255.0f
#define BLUR_BLEND_WEIGHT (128.0f / 255.0f)

static SDL_GPUSampler *
sky_sampler(const struct mh_gpu_renderer *gpu, const struct mh_mesh *mesh,
	const struct mh_draw *draw)
{
	if (mesh->environment_texture != 0 &&
		draw->texture == mesh->environment_texture)
		return gpu->repeat_sampler;

	return gpu->sky_sampler;
}

static int
draw_scene(struct mh_gpu_renderer *gpu, SDL_GPUCommandBuffer *command,
	SDL_GPURenderPass *pass, const float matrix[16], const float view[16],
	const float *models, int reflections, int blur, int direct3d)
{
	const enum mh_scene_effect *effect_order;
	struct mh_effect_policy policy;
	enum mh_render_profile profile;
	SDL_GPUBufferBinding vertex_binding;
	SDL_GPUTextureSamplerBinding texture_binding;
	const struct mh_draw *draw;
	const struct mh_mesh *mesh;
	struct mh_mesh_span span;
	struct mh_gpu_mesh *model;
	const float *transform;
	float camera_uniform[MH_GPU_CAMERA_FLOATS],
		material[MH_GPU_MATERIAL_FLOATS];
	size_t part, model_index, i, texture_index, image_index, first;
	int effect_index;
	enum mh_scene_effect effect;

	profile = gpu->powervr ? MH_RENDER_POWERVR
		: direct3d     ? MH_RENDER_DIRECT3D
			: MH_RENDER_GLIDE;
	effect_order = mh_scene_effect_order(profile, direct3d);
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	camera_uniform[MH_GPU_CAMERA_ASPECT] = (float)gpu->height / gpu->width;
	camera_uniform[MH_GPU_CAMERA_WIDTH] = gpu->width;
	camera_uniform[MH_GPU_CAMERA_HEIGHT] = gpu->height;
	camera_uniform[MH_GPU_CAMERA_POWERVR] = gpu->powervr ? 1.0f : 0.0f;

	mh_scene_fog_color(
		profile, gpu->fog.color, camera_uniform + MH_GPU_CAMERA_FOG);

	camera_uniform[MH_GPU_CAMERA_INVERSE_FAR] = gpu->fog.inverse_far;
	camera_uniform[MH_GPU_CAMERA_WATER_PHASE] = gpu->water.phase;
	camera_uniform[MH_GPU_CAMERA_WATER_SCALE] = gpu->water.scale;
	camera_uniform[MH_GPU_CAMERA_WATER_FREQUENCY] = gpu->water.frequency;
	memset(material, 0, sizeof(material));
	texture_binding.sampler = gpu->sampler;

	for (effect_index = 0;
		matrix != NULL && effect_index < MH_SCENE_EFFECT_PASSES;
		effect_index++) {
		effect = effect_order[effect_index];
		mh_scene_effect_policy(
			profile, direct3d, blur, effect, &policy);
		camera_uniform[MH_GPU_CAMERA_WATER_ENABLED] =
			effect != MH_SCENE_EFFECT_SKY && gpu->water_enabled
			? 1.0f
			: 0.0f;

		if (policy.material) {
			if (!mh_gpu_draw_materials(gpu, command, pass, matrix,
				view, models, reflections, effect, direct3d,
				camera_uniform))
				return 0;

			continue;
		}

		texture_binding.sampler = policy.repeat ? gpu->repeat_sampler
			: policy.sky_sampler		? gpu->sky_sampler
							: gpu->sampler;
		SDL_BindGPUGraphicsPipeline(
			pass, mh_gpu_pipeline(gpu, direct3d, policy.pipeline));

		for (part = 0; part < mh_gpu_scene_part_count(gpu); part++) {
			model_index = mh_gpu_scene_part(gpu, part, &span);
			model = &gpu->meshes[model_index];

			if (model->hidden || model->effect != effect)
				continue;

			mesh = model->mesh;

			transform = models == NULL ? mh_render_identity
						: models + model_index * 16;
			mh_render_matrix_multiply(
				camera_uniform, matrix, transform);
			mh_render_matrix_multiply(
				camera_uniform + MH_GPU_CAMERA_VIEW, view,
				transform);
			memcpy(camera_uniform + MH_GPU_CAMERA_MODEL, transform,
				16 * sizeof(float));

			if (effect == MH_SCENE_EFFECT_SKY ||
				effect == MH_SCENE_EFFECT_PARTICLES ||
				effect == MH_SCENE_EFFECT_HALOS) {
				memcpy(camera_uniform, mh_render_identity,
					16 * sizeof(float));
				memcpy(camera_uniform + MH_GPU_CAMERA_VIEW,
					mh_render_identity, 16 * sizeof(float));
				memcpy(camera_uniform + MH_GPU_CAMERA_MODEL,
					mh_render_identity, 16 * sizeof(float));
			}

			SDL_PushGPUVertexUniformData(command, 0, camera_uniform,
				sizeof(camera_uniform));
			vertex_binding.buffer = model->vertices;
			SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);

			for (i = span.draw_first;
				i < span.draw_first + span.draw_count; i++) {
				draw = &mesh->draws[i];

				if (mesh->draw_hidden != NULL &&
					mesh->draw_hidden[i])
					continue;

				if (policy.sky_sampler)
					texture_binding.sampler =
						sky_sampler(gpu, mesh, draw);

				material[MH_GPU_MATERIAL_ENVIRONMENT] = 0.0f;
				material[MH_GPU_MATERIAL_PRELIT] = 1.0f;
				material[MH_GPU_MATERIAL_FOG] = 0.0f;
				material[MH_GPU_MATERIAL_FLAT] =
					policy.flat ? 1.0f : 0.0f;
				material[MH_GPU_MATERIAL_OPACITY] =
					policy.opacity ? 1.0f : 0.0f;
				material[MH_GPU_MATERIAL_ALPHA] =
					policy.particle_alpha
					? MH_GPU_ALPHA_PARTICLE
					: 0.0f;
				SDL_PushGPUVertexUniformData(
					command, 1, material, sizeof(material));
				texture_index =
					effect == MH_SCENE_EFFECT_SKIDS ||
						effect ==
							MH_SCENE_EFFECT_SHADOWS
					? 0
					: draw->texture;
				image_index = model->images[texture_index];
				texture_binding.texture =
					gpu->images[image_index].texture;
				SDL_BindGPUFragmentSamplers(
					pass, 0, &texture_binding, 1);

				if (MH_RENDER_BUILD_POWERVR &&
					policy.primitive_vertices != 0) {
					if (draw->count %
							policy.primitive_vertices !=
						0)
						return SDL_SetError(
							"invalid PowerVR effect quads");

					for (first = draw->first; first <
						draw->first + draw->count;
						first +=
						policy.primitive_vertices)
						if (!mh_gpu_powervr_order_defer(
							gpu, model_index,
							first,
							policy.primitive_vertices,
							0, &texture_binding,
							camera_uniform,
							material))
							return 0;

					continue;
				}

				SDL_DrawGPUPrimitives(pass, (Uint32)draw->count,
					1, (Uint32)draw->first, 0);
			}
		}
	}

	if (MH_RENDER_BUILD_POWERVR && gpu->powervr)
		mh_gpu_powervr_order_draw(gpu, command, pass);

	return 1;
}

static int
render_frame(struct mh_gpu_renderer *gpu, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, int tron,
	const struct mh_renderer_tron_face *faces, size_t face_count,
	const struct mh_retro_draw *retro,
	const struct mh_projection *projection)
{
	SDL_GPUCommandBuffer *command;
	SDL_GPUTexture *swapchain;
	SDL_GPUTexture *target;
	SDL_FColor blend;
	SDL_GPURenderPass *pass;
	SDL_GPUColorTargetInfo color;
	SDL_GPUDepthStencilTargetInfo depth;
	SDL_GPUBlitInfo blit;
	Uint32 swapchain_width;
	Uint32 swapchain_height;
	size_t i;
	int blur, target_ready;

	if (gpu == NULL)
		return SDL_SetError("missing renderer");

	if (tron) {
		if (face_count > MH_TRON_MAX_FACES ||
			(face_count != 0 && faces == NULL))
			return SDL_SetError("invalid TRON frame");

		for (i = 0; i < face_count; i++) {
			if (faces[i].model >= gpu->mesh_count ||
				faces[i].texture >=
					gpu->meshes[faces[i].model]
						.mesh->texture_count)
				return SDL_SetError("invalid TRON texture");
		}

		if (gpu->tron == NULL)
			gpu->tron = mh_gpu_tron_create(
				gpu->device, gpu->width, gpu->height);

		if (gpu->tron == NULL ||
			!mh_gpu_tron_prepare(gpu->tron, faces, face_count))
			return 0;
	} else if (matrix != NULL &&
		!mh_gpu_prepare_materials(
			gpu, matrix, view, models, reflections, projection))
		return 0;

	if (MH_RENDER_BUILD_POWERVR && matrix != NULL &&
		!mh_gpu_powervr_order_begin(gpu))
		return 0;

	if (ui != NULL && !mh_gpu_ui_upload_vertices(gpu, ui))
		return 0;

	if (retro != NULL) {
		if (retro->width != gpu->width || retro->height != gpu->height)
			return SDL_SetError(
				"retro frame does not match viewport");

		if (gpu->ui_texture == NULL || gpu->ui_sampler == NULL)
			return SDL_SetError("missing retro font atlas");

		if (gpu->retro == NULL)
			gpu->retro = mh_gpu_retro_create(gpu->device);

		if (gpu->retro == NULL ||
			!mh_gpu_retro_prepare(gpu->retro, retro))
			return 0;
	}

	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL) {
		return 0;
	}

	swapchain = NULL;
	swapchain_width = 0;
	swapchain_height = 0;

	if (gpu->window != NULL &&
		!SDL_WaitAndAcquireGPUSwapchainTexture(command, gpu->window,
			&swapchain, &swapchain_width, &swapchain_height)) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&color, 0, sizeof(color));
	target = gpu->back_color != NULL ? gpu->back_color : gpu->color;
	target_ready = gpu->back_color != NULL ? gpu->back_color_ready
					: gpu->color_ready;
	blur = matrix != NULL && gpu->motion_blur;
	color.texture = target;
	color.clear_color.r = matrix == NULL ? 0 : 0.055f;
	color.clear_color.g = matrix == NULL ? 0 : 0.070f;
	color.clear_color.b = matrix == NULL ? 0 : 0.095f;
	color.clear_color.a = 1.0f;

	/* Fallback behind the optional original sky mesh. */
	if (matrix != NULL && gpu->fog_enabled) {
		color.clear_color.r = gpu->fog.color[0];
		color.clear_color.g = gpu->fog.color[1];
		color.clear_color.b = gpu->fog.color[2];
	}

	color.load_op = SDL_GPU_LOADOP_CLEAR;
	color.store_op = SDL_GPU_STOREOP_STORE;
	color.cycle = true;

	if (blur) {
		/*
		 * Retain the complete frame from two swaps earlier (Glide and
		 * default Direct3D), or three with Direct3D's triple-buffer
		 * choice. First use starts black. Cycling a loaded SDL target
		 * would discard the history we need to blend.
		 */
		color.clear_color.r = color.clear_color.g =
			color.clear_color.b = 0;
		color.load_op = target_ready ? SDL_GPU_LOADOP_LOAD
					: SDL_GPU_LOADOP_CLEAR;
		color.cycle = false;
	}

	if (gpu->back_color != NULL)
		gpu->back_color_ready = 0;
	else
		gpu->color_ready = 0;
	memset(&depth, 0, sizeof(depth));
	depth.texture = gpu->depth;
	depth.clear_depth = 1.0f;
	depth.load_op = SDL_GPU_LOADOP_CLEAR;

	/* Source visibility reads follow the frame. */
	depth.store_op = SDL_GPU_STOREOP_STORE;
	depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.cycle = true;
	pass = SDL_BeginGPURenderPass(command, &color, 1, &depth);

	if (pass == NULL) {
		/*
		 * An acquired swapchain prevents cancelling this command
		 * buffer.
		 */
		SDL_SubmitGPUCommandBuffer(command);
		return 0;
	}

	SDL_BindGPUGraphicsPipeline(
		pass, mh_gpu_pipeline(gpu, 0, MH_GPU_PIPELINE_OPAQUE));
	blend.r = blend.g = blend.b = BLUR_BLEND_WEIGHT;
	blend.a = 1;

	if (blur)
		SDL_SetGPUBlendConstants(pass, blend);

	if (tron)
		for (i = 0; i < face_count; i++)
			mh_gpu_tron_draw(gpu->tron, command, pass, i,
				gpu->images[gpu->meshes[faces[i].model]
						.images[faces[i].texture]]
					.texture);

	if (matrix != NULL &&
		!draw_scene(gpu, command, pass, matrix, view, models,
			reflections, blur, projection != NULL)) {
		SDL_EndGPURenderPass(pass);
		SDL_SubmitGPUCommandBuffer(command);
		return 0;
	}

	SDL_EndGPURenderPass(pass);

	if (ui != NULL && ui->count != 0 &&
		!mh_gpu_ui_draw(gpu, command, &color, ui)) {
		SDL_SubmitGPUCommandBuffer(command);
		return 0;
	}

	if (retro != NULL) {
		color.load_op = SDL_GPU_LOADOP_LOAD;
		color.cycle = false;
		depth.load_op = SDL_GPU_LOADOP_LOAD;
		depth.cycle = false;
		pass = SDL_BeginGPURenderPass(command, &color, 1, &depth);

		if (pass == NULL) {
			SDL_SubmitGPUCommandBuffer(command);
			return 0;
		}

		mh_gpu_retro_draw(gpu->retro, command, pass, gpu->ui_texture,
			gpu->ui_sampler, retro);
		SDL_EndGPURenderPass(pass);
	}

	if (swapchain != NULL) {
		memset(&blit, 0, sizeof(blit));
		blit.source.texture = target;
		blit.source.w = gpu->width;
		blit.source.h = gpu->height;
		blit.destination.texture = swapchain;
		blit.destination.w = swapchain_width;
		blit.destination.h = swapchain_height;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_LINEAR;
		SDL_BlitGPUTexture(command, &blit);
	}

	if (!SDL_SubmitGPUCommandBuffer(command))
		return 0;

	if (gpu->back_color != NULL) {
		if (gpu->third_color != NULL) {
			gpu->back_color = gpu->third_color;
			gpu->back_color_ready = gpu->third_color_ready;
			gpu->third_color = gpu->color;
			gpu->third_color_ready = gpu->color_ready;
		} else {
			gpu->back_color = gpu->color;
			gpu->back_color_ready = gpu->color_ready;
		}

		gpu->color = target;
	}

	gpu->color_ready = 1;

	return 1;
}

int
mh_gpu_renderer_scene_frame_matrix(struct mh_gpu_renderer *gpu,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro)
{
	return render_frame(gpu, matrix, view, models, reflections, ui, 0, NULL,
		0, retro, NULL);
}

int
mh_gpu_renderer_scene_frame_lens(struct mh_gpu_renderer *gpu,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro)
{
	float lens[16], matrix[16];

	if (gpu == NULL || projection == NULL || view == NULL ||
		projection->clip.extent[0] != gpu->width ||
		projection->clip.extent[1] != gpu->height ||
		!(projection->clip.near_distance > 0))
		return SDL_SetError("invalid GPU scene lens");

	memset(lens, 0, sizeof(lens));
	lens[0] = projection->clip.focal;
	lens[5] = projection->clip.focal * projection->clip.aspect *
		gpu->width / gpu->height;

	/* Prepared faces use original near/Z reversed depth. */
	lens[10] = -1;
	lens[11] = -1;
	lens[14] = -projection->clip.near_distance;
	mh_render_matrix_multiply(matrix, lens, view);

	return render_frame(gpu, matrix, view, models, reflections, ui, 0, NULL,
		0, retro, projection);
}

int
mh_gpu_renderer_tron_frame_ui(struct mh_gpu_renderer *gpu,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui)
{
	return render_frame(
		gpu, NULL, NULL, NULL, 0, ui, 1, faces, count, NULL, NULL);
}
