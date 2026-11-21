#ifndef MH_WEBGL_BACKEND_H
#define MH_WEBGL_BACKEND_H

#include "interfaces/render/backend.h"

/* Shared operations and construction used by the profile entry modules. */
extern const struct mh_render_ops mh_webgl_ops;
enum mh_error_code	mh_webgl_open(const void *context,
	struct mh_window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height,
	unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out);

#endif
