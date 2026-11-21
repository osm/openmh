#ifndef MH_GPU_GLENZ_H
#define MH_GPU_GLENZ_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/*
 * Copies coordinates and publishes material choices after upload submission.
 * Partial allocations remain renderer-owned on failure. NULL disables the
 * material without releasing its reusable buffers.
 */
enum mh_error_code	mh_gpu_mesh_glenz(struct mh_renderer *renderer,
	size_t index, const struct mh_renderer_glenz *glenz,
	struct mh_error *error_out);

#endif
