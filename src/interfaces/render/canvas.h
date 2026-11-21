#ifndef MH_INTERFACES_RENDER_CANVAS_H
#define MH_INTERFACES_RENDER_CANVAS_H

#include "engine/assets/image.h"
#include "engine/core/error.h"

struct mh_window;
struct mh_canvas;
struct mh_canvas_provider;

/*
 * Keep the source image at its own dimensions and place it inside a logical
 * screen. The remainder is black. Scaling happens only when that logical
 * screen is fitted into the window, retaining the source texture's sampling.
 */
struct mh_canvas_layout {
	unsigned int	width, height;
	unsigned int	screen_width, screen_height;
	unsigned int	x, y;
};

/*
 * Present tightly packed RGBA8 with nearest filtering and a black letterbox.
 * Provider and window are borrowed until destroy. Dimensions remain fixed;
 * window resizing changes only the presentation area. Logical game dimensions
 * are independent of window coordinates and drawable pixels. The backend owns
 * their conversion and any host-selected usable area/letterbox; game layout
 * does not inspect native DPI, orientation or safe-area APIs. Failed open
 * leaves output unchanged. Errors belong to the operation, with provider
 * messages borrowed until its next operation; copy diagnostics before cleanup.
 */
enum mh_error_code	mh_canvas_open(
	const struct mh_canvas_provider *provider, struct mh_window *window,
	unsigned int width, unsigned int height, struct mh_canvas **output,
	struct mh_error *error_out);

/* Layout is copied during open; the source rectangle must fit the screen. */
enum mh_error_code	mh_canvas_open_layout(
	const struct mh_canvas_provider *provider, struct mh_window *window,
	const struct mh_canvas_layout *layout, struct mh_canvas **output,
	struct mh_error *error_out);
void			mh_canvas_destroy(struct mh_canvas *canvas);

/* Pixel storage has width*height*4 bytes and is borrowed during this call. */
enum mh_error_code	mh_canvas_draw(struct mh_canvas *canvas,
	const unsigned char *pixels, struct mh_error *error_out);
enum mh_error_code	mh_canvas_present(struct mh_canvas *canvas,
	struct mh_error *error_out);

/*
 * Read the prepared window-sized image before present, including letterbox.
 * Caller frees output->rgba. Failure leaves output unchanged; absent readback
 * reports UNSUPPORTED. No file access or event-loop wait is required.
 */
enum mh_error_code	mh_canvas_read(struct mh_canvas *canvas,
	struct mh_image *output, struct mh_error *error_out);

/* Optional window presentation control; enabled is 0 or 1. */
enum mh_error_code	mh_canvas_set_vsync(struct mh_canvas *canvas,
	int enabled, struct mh_error *error_out);

#endif
