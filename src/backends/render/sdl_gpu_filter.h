#ifndef MH_GPU_FILTER_H
#define MH_GPU_FILTER_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

enum mh_error_code	mh_gpu_set_texture_filter(struct mh_renderer *renderer,
	enum mh_texture_filter filter, struct mh_error *error_out);

/* Partial sampler creation remains renderer-owned on failure. */
int		 mh_gpu_create_filters(struct mh_gpu_renderer *gpu);
SDL_GPUSampler	*mh_gpu_material_sampler(struct mh_gpu_renderer *gpu,
	int direct3d, int glenz, int environment, int alpha);

#endif
