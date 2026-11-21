#include <string.h>

#include "backends/render/sdl_gpu_filter.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "engine/render/scene_material_policy.h"

int
mh_gpu_create_filters(struct mh_gpu_renderer *gpu)
{
	SDL_GPUSamplerCreateInfo info;
	unsigned int filter, slot;

	memset(&info, 0, sizeof(info));
	info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

	for (filter = MH_TEXTURE_FILTER_NEAREST;
		filter <= MH_TEXTURE_FILTER_TRILINEAR; filter++) {
		slot = filter - MH_TEXTURE_FILTER_NEAREST;
		info.min_filter = info.mag_filter =
			filter == MH_TEXTURE_FILTER_NEAREST
			? SDL_GPU_FILTER_NEAREST
			: SDL_GPU_FILTER_LINEAR;
		info.mipmap_mode = filter == MH_TEXTURE_FILTER_TRILINEAR
			? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
			: SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		info.max_lod = filter == MH_TEXTURE_FILTER_TRILINEAR
			? MH_TEXTURE_MIP_LEVELS - 1
			: 0;
		info.address_mode_u = info.address_mode_v =
			SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		gpu->filter_samplers[slot] =
			SDL_CreateGPUSampler(gpu->device, &info);
		info.address_mode_u = info.address_mode_v =
			SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		gpu->repeat_filters[slot] =
			SDL_CreateGPUSampler(gpu->device, &info);

		if (gpu->filter_samplers[slot] == NULL ||
			gpu->repeat_filters[slot] == NULL)
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_gpu_set_texture_filter(struct mh_renderer *renderer,
	enum mh_texture_filter filter, struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;

	gpu = (struct mh_gpu_renderer *)renderer;
	gpu->texture_filter = filter;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

SDL_GPUSampler *
mh_gpu_material_sampler(struct mh_gpu_renderer *gpu, int direct3d, int glenz,
	int environment, int alpha)
{
	struct mh_material_sampling sampling;
	enum mh_render_profile profile;

	profile = gpu->powervr ? MH_RENDER_POWERVR
		: direct3d     ? MH_RENDER_DIRECT3D
			: MH_RENDER_GLIDE;
	sampling = mh_scene_material_sampling(
		profile, gpu->texture_filter, glenz, environment, alpha);

	return sampling.repeat ? gpu->repeat_filters[sampling.filter -
					MH_TEXTURE_FILTER_NEAREST]
			: gpu->filter_samplers[sampling.filter -
					MH_TEXTURE_FILTER_NEAREST];
}
