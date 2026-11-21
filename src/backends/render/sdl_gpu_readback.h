#ifndef MH_GPU_READBACK_H
#define MH_GPU_READBACK_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/*
 * Active batches copy probes and allow only one unread batch. Reads update
 * active entries only. With no pending batch, visibility is unchanged.
 */
int	mh_gpu_renderer_depth_queue(struct mh_gpu_renderer *renderer,
	const struct mh_depth_probe *probes, size_t count);
int	mh_gpu_renderer_depth_read(struct mh_gpu_renderer *renderer,
	unsigned char *visible, size_t count);
int	mh_gpu_renderer_capture(struct mh_gpu_renderer *renderer,
	const char *path);

/* Encoded picture owns output->data; the caller frees it. */
int	mh_gpu_renderer_tga_capture(struct mh_gpu_renderer *renderer,
	struct mh_file *output);

int	mh_gpu_renderer_read_pixels(struct mh_gpu_renderer *renderer,
	struct mh_image *output);

#endif
