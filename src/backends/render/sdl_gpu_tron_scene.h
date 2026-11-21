#ifndef MH_GPU_TRON_SCENE_H
#define MH_GPU_TRON_SCENE_H

#include "backends/render/sdl_gpu_renderer.h"

struct mh_gpu_renderer;

/* Failed preparation may leave a partial retained face list. */
int	mh_gpu_renderer_tron_scene_ui(struct mh_gpu_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui);

#endif
