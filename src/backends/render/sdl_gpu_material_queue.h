#ifndef MH_GPU_MATERIAL_QUEUE_H
#define MH_GPU_MATERIAL_QUEUE_H

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/render/material_lists.h"

struct mh_gpu_renderer;

/*
 * Original order: distant solid/cutout, solid, registered texture groups,
 * environment, 200 alpha buckets, then unavailable after particles.
 * Unregistered inspection meshes keep their existing opaque batching.
 */

/*
 * Starts fresh lists after range validation; failure may retain partial work.
 */
int	mh_gpu_prepare_materials(struct mh_gpu_renderer *gpu,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_projection *projection);
int	mh_gpu_queue_material(struct mh_gpu_renderer *gpu, size_t model,
	size_t draw, size_t first, size_t count, size_t list, int near_clipped,
	int prepared);

#endif
