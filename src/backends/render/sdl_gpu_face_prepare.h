#ifndef MH_GPU_FACE_PREPARE_H
#define MH_GPU_FACE_PREPARE_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/* Appends frame scratch and list entries; failure may retain partial work. */
int	mh_gpu_face_prepare(struct mh_gpu_renderer *gpu, size_t model,
	size_t draw, size_t first, size_t count, unsigned int type,
	const float camera[16], const struct mh_projection *projection,
	int reflections);

/* Uploads positions/attributes followed by the separate Glenz stream. */
int	mh_gpu_upload_prepared(struct mh_gpu_renderer *gpu);

#endif
