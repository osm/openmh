#ifndef MH_BACKENDS_RENDER_SOFTWARE_PROVIDER_H
#define MH_BACKENDS_RENDER_SOFTWARE_PROVIDER_H

#include "interfaces/render/backend.h"
#include "interfaces/render/canvas.h"

/*
 * Host-selected output for the CPU renderer. This configuration and its canvas
 * provider remain borrowed unchanged through renderer destruction. NULL canvas
 * support permits offscreen rendering only. Capture is an optional diagnostic
 * sink: pixels/path are borrowed during the callback, no filesystem is
 * required.
 */
struct mh_software_output {
	const struct mh_canvas_provider	*canvases;
	enum mh_error_code (*capture)(void *context,
		const struct mh_image *image, const char *path,
		struct mh_error		*error_out);
	void				*context;
};

/* Offscreen CPU renderer; configure window output via this header. */
const struct mh_render_backend	*mh_software_backend(void);

/* No I/O; NULL output selects offscreen-only operation. */
enum mh_error_code	mh_software_backend_init(
	struct mh_render_backend *backend,
	const struct mh_software_output *output, struct mh_error *error_out);

#endif
