#ifndef MH_GPU_UI_H
#define MH_GPU_UI_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/* Copies this frame's vertices into reusable GPU storage. */
int	mh_gpu_ui_upload_vertices(struct mh_gpu_renderer *gpu,
	const struct mh_ui_list *ui);

/* The caller submits the command on a failed overlay pass. */
int	mh_gpu_ui_draw(struct mh_gpu_renderer *gpu,
	SDL_GPUCommandBuffer *command, SDL_GPUColorTargetInfo *color,
	const struct mh_ui_list *ui);

/* Copies atlas pixels; partial GPU resources stay owned for destruction. */
int	mh_gpu_renderer_ui_upload(struct mh_gpu_renderer *renderer,
	const struct mh_ui_atlas *atlas);

#endif
