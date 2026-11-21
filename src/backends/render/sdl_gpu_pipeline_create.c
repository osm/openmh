#include <stddef.h>
#include <string.h>

#include "backends/render/sdl_gpu_pipeline_create.h"
#include "backends/render/sdl_gpu_vertex_input.h"

static SDL_GPUBlendFactor
blend_factor(enum mh_raster_factor factor)
{
	static const SDL_GPUBlendFactor factors[] = {SDL_GPU_BLENDFACTOR_ZERO,
		SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_SRC_ALPHA,
		SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
		SDL_GPU_BLENDFACTOR_DST_COLOR,
		SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,
		SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR};

	return factors[factor];
}

static void
apply_state(SDL_GPUGraphicsPipelineCreateInfo *info,
	SDL_GPUColorTargetDescription *target,
	const struct mh_raster_state *state)
{
	info->depth_stencil_state.enable_depth_test = state->depth_test != 0;

	info->depth_stencil_state.enable_depth_write = state->depth_write != 0;
	info->depth_stencil_state.compare_op = state->depth_always
		? SDL_GPU_COMPAREOP_ALWAYS
		: SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	info->rasterizer_state.enable_depth_bias = state->depth_bias != 0;
	info->rasterizer_state.depth_bias_constant_factor = state->depth_bias;
	target->blend_state.enable_blend = state->blend != 0;
	target->blend_state.src_color_blendfactor =
		blend_factor(state->source_color);
	target->blend_state.dst_color_blendfactor =
		blend_factor(state->destination_color);
	target->blend_state.src_alpha_blendfactor =
		blend_factor(state->source_alpha);
	target->blend_state.dst_alpha_blendfactor =
		blend_factor(state->destination_alpha);
	target->blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	target->blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
}

static int
create_set(SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set,
	enum mh_gpu_shader vertex_id, enum mh_gpu_shader glenz_id,
	SDL_GPUShader *vertex, SDL_GPUShader *fragment, SDL_GPUShader *glenz,
	enum mh_error_code (*describe)(enum mh_gpu_pipeline,
		struct mh_raster_state *, struct mh_error *))
{
	SDL_GPUGraphicsPipelineCreateInfo info;
	struct mh_gpu_vertex_input vertex_input;
	SDL_GPUColorTargetDescription target;
	struct mh_raster_state state;
	unsigned int pipeline;
	int environment, success;

	memset(&info, 0, sizeof(info));
	memset(&target, 0, sizeof(target));
	target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.fragment_shader = fragment;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.enable_depth_clip = true;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	success = 1;

	for (pipeline = 0; pipeline < MH_GPU_PIPELINE_COUNT; pipeline++) {
		if (describe((enum mh_gpu_pipeline)pipeline, &state, NULL) !=
			MH_ERROR_NONE)
			return SDL_SetError("cannot describe render pipeline");

		environment = pipeline == MH_GPU_PIPELINE_REFLECTION ||
			pipeline == MH_GPU_PIPELINE_PHONG;
		info.vertex_shader = environment ? glenz : vertex;
		info.vertex_input_state = *mh_gpu_vertex_input(
			environment ? glenz_id : vertex_id, &vertex_input);
		apply_state(&info, &target, &state);
		set->items[pipeline] =
			SDL_CreateGPUGraphicsPipeline(device, &info);

		if (set->items[pipeline] == NULL)
			success = 0;
	}

	return success;
}

int
mh_gpu_pipeline_set_create(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set, enum mh_gpu_shader vertex_id,
	enum mh_gpu_shader fragment_id, enum mh_gpu_shader glenz_id,
	enum mh_error_code (*state)(enum mh_gpu_pipeline,
		struct mh_raster_state *, struct mh_error *))
{
	SDL_GPUShader *vertex, *fragment, *glenz;
	int success;

	fragment = NULL;
	glenz = NULL;
	success = 0;
	vertex = mh_gpu_shader_create(device, vertex_id);

	if (vertex == NULL)
		goto done;

	fragment = mh_gpu_shader_create(device, fragment_id);

	if (fragment == NULL)
		goto done;

	glenz = mh_gpu_shader_create(device, glenz_id);

	if (glenz != NULL)
		success = create_set(device, set, vertex_id, glenz_id, vertex,
			fragment, glenz, state);

done:
	if (glenz != NULL)
		SDL_ReleaseGPUShader(device, glenz);

	if (fragment != NULL)
		SDL_ReleaseGPUShader(device, fragment);

	if (vertex != NULL)
		SDL_ReleaseGPUShader(device, vertex);

	return success;
}
