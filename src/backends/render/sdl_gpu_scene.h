#ifndef MH_GPU_SCENE_H
#define MH_GPU_SCENE_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/* Updates ranges in mesh order; earlier updates remain on failure. */
int	mh_gpu_update_ranges(struct mh_gpu_renderer *gpu);
size_t	mh_gpu_scene_part_count(const struct mh_gpu_renderer *gpu);

/* Resolves a part to its mesh index and current active or fixed span. */
size_t	mh_gpu_scene_part(const struct mh_gpu_renderer *gpu, size_t index,
	struct mh_mesh_span *span);

/* Copies the order before replacing it; failure preserves the current order. */
int	mh_gpu_renderer_scene_order(struct mh_gpu_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count);

#endif
