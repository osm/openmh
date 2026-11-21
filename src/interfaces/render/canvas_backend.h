#ifndef MH_RENDER_CANVAS_BACKEND_H
#define MH_RENDER_CANVAS_BACKEND_H

#include "interfaces/render/canvas.h"

/*
 * Descriptor, operations and context are borrowed unchanged through the last
 * canvas close. Calls run on the host's presentation thread. Each canvas owns
 * a provider token and borrows its window; close canvases before their windows.
 * Providers reject incompatible windows before accessing native handles.
 */
struct mh_canvas_provider {
	const struct mh_canvas_ops	*ops;
	void				*context;
};

struct mh_canvas_ops {
	/*
	 * Layout is borrowed for this call only; copy values needed later.
	 * Failed open releases partial state and leaves output unchanged.
	 */
	enum mh_error_code (*open)(void *context, struct mh_window *window,
		const struct mh_canvas_layout *layout, void **output,
		struct mh_error		*error_out);
	void				(*close)(void *context, void *canvas);
	enum mh_error_code (*draw)(void *context, void *canvas,
		const unsigned char	*pixels, struct mh_error *error_out);
	enum mh_error_code (*present)(
		void	*context, void *canvas, struct mh_error *error_out);

	/* Optional; caller frees output->rgba, output unchanged on failure. */
	enum mh_error_code (*read)(void *context, void *canvas,
		struct mh_image		*output, struct mh_error *error_out);

	/* Optional; enabled is 0 or 1. */
	enum mh_error_code (*set_vsync)(void *context, void *canvas,
		int			enabled, struct mh_error *error_out);
};

/* No I/O. Missing required operations leave provider unchanged. */
enum mh_error_code	mh_canvas_provider_init(
	struct mh_canvas_provider *provider, const struct mh_canvas_ops *ops,
	void *context, struct mh_error *error_out);
#endif
