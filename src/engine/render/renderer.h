#ifndef MH_ENGINE_RENDER_RENDERER_H
#define MH_ENGINE_RENDER_RENDERER_H

#include "interfaces/render/backend.h"

/* Legacy boolean operations retain diagnostics on their renderer owner. */
#define MH_RENDERER_ERROR_SIZE 512

/*
 * Implementers embed this as their first member. The implementation owns the
 * allocation and destroy releases it. Callers only see the opaque handle.
 * The backend descriptor, operation table and context must outlive every
 * renderer and remain unchanged through destruction.
 */
struct mh_renderer {
	const struct mh_render_backend	*backend;
	struct mh_error			error;
	char				error_message[MH_RENDERER_ERROR_SIZE];
};

#endif
