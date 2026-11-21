#ifndef MH_WEBGL_CONTEXT_H
#define MH_WEBGL_CONTEXT_H

#include <emscripten/html5_webgl.h>

#include "engine/core/error.h"

struct mh_window;

struct mh_webgl_context {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	handle;
	struct mh_window		*window;

	/*
	 * Called with this context current after browser restoration. Rebuild
	 * only GPU resources; do not call context_current recursively. Failure
	 * leaves restoration pending, so the callback must support retries.
	 */
	enum mh_error_code (*restore)(
		void			*context, struct mh_error *error_out);
	void				*restore_context;
};

/*
 * Acquire exclusive use of the window's WebGL 2 context. The window owns the
 * underlying context until window destruction; renderer close releases only
 * its acquisition. Use the WebGL canvas provider on these windows, not SDL's
 * renderer. The borrowed window must outlive each acquisition.
 * Zero-initialize output and keep its address stable until close; failed open
 * leaves it unchanged.
 */
enum mh_error_code	mh_webgl_context_open(struct mh_webgl_context *output,
	struct mh_window *window, struct mh_error *error_out);
enum mh_error_code	mh_webgl_context_current(
	const struct mh_webgl_context *context, struct mh_error *error_out);
enum mh_error_code	mh_webgl_context_size(
	const struct mh_webgl_context *context, unsigned int *width,
	unsigned int *height, struct mh_error *error_out);

/* Clear restore before teardown; release GPU resources before closing. */
void	mh_webgl_context_close(struct mh_webgl_context *context);

#endif
