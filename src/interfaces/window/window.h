#ifndef MH_INTERFACES_WINDOW_WINDOW_H
#define MH_INTERFACES_WINDOW_WINDOW_H

#include "engine/core/error.h"

struct mh_window;
struct mh_window_system;
struct mh_image;

struct mh_window_system_options {
	int	offscreen;
	int	preserve_environment;
};

/* Return zero to stop enumeration; dimensions are pixels, not coordinates. */
typedef int (*mh_display_mode_visit)(
	void *context, unsigned int width, unsigned int height);

enum mh_window_flags {
	MH_WINDOW_HIDDEN = 1,
	MH_WINDOW_RESIZABLE = 2,
	MH_WINDOW_FULLSCREEN = 4,
	MH_WINDOW_DIAGNOSTIC_TITLES = 8
};

/* Whether this window was opened with diagnostic captions enabled. */
int	mh_window_diagnostic_titles(const struct mh_window *window);

/*
 * Acquire the selected system before opening windows or enumerating displays.
 * NULL options retains the host's ordinary display choice. Offscreen support
 * is optional; requesting it may fail. Release after all dependent owners.
 * Errors use this operation's diagnostic, without a global last-error query.
 * Provider messages are borrowed until its next operation on this thread;
 * copy diagnostics before cleanup when they must survive it.
 */
enum mh_error_code	mh_window_system_acquire(
	const struct mh_window_system *system,
	const struct mh_window_system_options *options,
	struct mh_error *error_out);
void			mh_window_system_release(
	const struct mh_window_system *system);

/* Whether creation and mode switching support borderless fullscreen. */
int	mh_window_system_has_fullscreen(const struct mh_window_system *system);

/*
 * Optional primary-display sizes, at most limit callbacks. A failed scan may
 * already have visited sizes. No display IDs, placement, fullscreen switch or
 * focus guarantee is required. Missing support reports UNSUPPORTED.
 */
enum mh_error_code	mh_window_system_modes(
	const struct mh_window_system *system, unsigned int limit,
	mh_display_mode_visit visit, void *context, struct mh_error *error_out);

/*
 * Own a window borrowing its creating system until destruction. Failed open
 * leaves output unchanged. Close input/presentation users before the window.
 * IDs identify this provider's events; they are not native handles.
 * Width/height request window-coordinate dimensions, not a guaranteed drawable
 * pixel size. The presentation backend queries native drawable dimensions;
 * game render targets retain their independently selected logical resolution.
 */
enum mh_error_code	mh_window_open(const struct mh_window_system *system,
	const char *title, unsigned int width, unsigned int height,
	unsigned int flags, struct mh_window **output,
	struct mh_error *error_out);
enum mh_error_code	mh_window_set_visible(struct mh_window *window,
	int visible, struct mh_error *error_out);
enum mh_error_code	mh_window_request_raise(struct mh_window *window,
	struct mh_error *error_out);
enum mh_error_code	mh_window_set_title(struct mh_window *window,
	const char *title, struct mh_error *error_out);

/* Optional RGBA icon. Pixels are borrowed only for the duration of the call. */
enum mh_error_code	mh_window_set_icon(struct mh_window *window,
	const struct mh_image *image, struct mh_error *error_out);

/*
 * Optional window-coordinate size, independent of logical render resolution.
 * Queries leave outputs unchanged on failure. Resize requests may be
 * constrained by the desktop; presentation still queries the actual drawable
 * dimensions. The client resizes windowed activities only, retaining borderless
 * fullscreen.
 */
enum mh_error_code	mh_window_get_size(struct mh_window *window,
	unsigned int *width, unsigned int *height, struct mh_error *error_out);
enum mh_error_code	mh_window_set_size(struct mh_window *window,
	unsigned int width, unsigned int height, struct mh_error *error_out);

/*
 * Borderless fullscreen uses the current display mode. Leaving restores the
 * windowed size. A successful request may complete asynchronously; drawable
 * sizes must still be queried by presentation. Missing support is UNSUPPORTED.
 */
enum mh_error_code	mh_window_set_fullscreen(struct mh_window *window,
	int fullscreen, struct mh_error *error_out);

void		mh_window_destroy(struct mh_window *window);
unsigned long	mh_window_id(const struct mh_window *window);

#endif
