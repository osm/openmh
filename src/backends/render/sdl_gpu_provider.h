#ifndef MH_BACKENDS_RENDER_SDL_GPU_PROVIDER_H
#define MH_BACKENDS_RENDER_SDL_GPU_PROVIDER_H

#include "interfaces/render/backend.h"

/* Rendering profiles implemented with SDL GPU. */
const struct mh_render_backend	*mh_sdl_gpu_glide_backend(void);
const struct mh_render_backend	*mh_sdl_gpu_direct3d_backend(void);
const struct mh_render_backend	*mh_sdl_gpu_powervr_backend(void);

const struct mh_render_backend	*mh_sdl_gpu_openmh_backend(void);

#endif
