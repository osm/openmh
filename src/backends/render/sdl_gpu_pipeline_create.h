#ifndef MH_SDL_PIPELINE_CREATE_H
#define MH_SDL_PIPELINE_CREATE_H

#include "backends/render/sdl_gpu_pipelines.h"
#include "backends/render/sdl_gpu_shaders.h"

/* Partial pipeline sets remain owned by the renderer on failure. */
int	mh_gpu_pipeline_set_create(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set, enum mh_gpu_shader vertex_id,
	enum mh_gpu_shader fragment_id, enum mh_gpu_shader glenz_id,
	enum mh_error_code (*state)(enum mh_gpu_pipeline, struct mh_raster_state *, struct mh_error *));

#endif
