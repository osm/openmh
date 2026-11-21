#ifndef MH_GPU_FRAME_H
#define MH_GPU_FRAME_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/*
 * Frame calls borrow inputs and own command submission, including failures
 * after swapchain acquisition. History rotates only after successful submit.
 */
int	mh_gpu_renderer_scene_frame_lens(struct mh_gpu_renderer *gpu,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro);
int	mh_gpu_renderer_scene_frame_matrix(struct mh_gpu_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro);
int	mh_gpu_renderer_tron_frame_ui(struct mh_gpu_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui);

#endif
