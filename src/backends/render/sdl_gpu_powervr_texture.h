#ifndef MH_GPU_POWERVR_TEXTURE_H
#define MH_GPU_POWERVR_TEXTURE_H

#include "backends/render/sdl_gpu_renderer_internal.h"

/* Failure leaves dimensions initialized to the borrowed source image size. */
int	mh_gpu_powervr_image_size_prepare(struct mh_gpu_image *image);

/*
 * Recomputes UVs from source vertices; earlier draws remain changed on failure.
 */
int	mh_gpu_powervr_sky_uv_scale(const struct mh_gpu_renderer *gpu,
	const struct mh_gpu_mesh *model, struct mh_vertex *vertices);

#endif
