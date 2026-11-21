#ifndef MH_GPU_DEVICE_H
#define MH_GPU_DEVICE_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

enum mh_error_code	mh_gpu_set_frame_queue(struct mh_renderer *renderer,
	unsigned int frames, struct mh_error *error_out);

SDL_GPUDevice	*mh_gpu_device_create(int diagnostics);

enum mh_error_code	mh_gpu_set_vsync(struct mh_renderer *renderer,
	int enabled, struct mh_error *error_out);

#endif
