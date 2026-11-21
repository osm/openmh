#ifndef MH_SDL_GPU_BACKEND_H
#define MH_SDL_GPU_BACKEND_H

#include "interfaces/render/backend.h"

/* Shared operations and construction used by the profile entry modules. */
extern const struct mh_render_ops mh_sdl_gpu_ops;
enum mh_error_code	mh_sdl_gpu_open(const void *context,
	struct mh_window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height,
	unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out);

#endif
