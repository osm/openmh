#ifndef MH_CLIENT_LOADING_VIEW_H
#define MH_CLIENT_LOADING_VIEW_H

#include "client/loading/confirmation.h"
#include "engine/ui/loading_screen.h"

struct mh_debug_options;

struct mh_loading_view;
struct mh_image_decoder_provider;
struct mh_font_source_provider;
struct mh_render_backend;
struct mh_resource_source;
struct mh_window;
struct mh_window_system;

/*
 * Borrows window, or opens its own when NULL, and submits the loading picture.
 * A borrowed window must have no active canvas or renderer and outlive close.
 * Resources are borrowed during open; backend and windows through close.
 * No game clock or random stream is sampled, and no delay is introduced.
 * Failed open leaves output unchanged and releases partial ownership.
 */
enum mh_error_code	mh_loading_view_open(
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_window_system *windows, struct mh_window *window,
	const struct mh_render_backend *backend, unsigned int width,
	unsigned int height, int fullscreen,
	const struct mh_loading_layout *layout,
	const struct mh_debug_options *debug, struct mh_loading_view **output,
	struct mh_error *error_out);

/* Advance a confirmed milestone; only software redraws the indicators. */
enum mh_error_code	mh_loading_view_step(struct mh_loading_view *view,
	unsigned long milliseconds, struct mh_error *error_out);

/*
 * Race startup acquires its own window-system reference, then takes this
 * window and its ownership flag. Borrowed windows stay client-owned.
 * Retire the loading renderer before the race renderer claims it.
 * The view remains owned and must still be closed, even after transfer.
 * Failed compatibility checks leave both owners unchanged.
 */
enum mh_error_code	mh_loading_view_take_window(
	struct mh_loading_view *view, const struct mh_window_system *windows,
	enum mh_render_profile profile, unsigned int width, unsigned int height,
	struct mh_window **output, int *owns_window,
	struct mh_error *error_out);

/* Retain while a race waits; the session may release its own reference. */
void			mh_loading_view_retain(struct mh_loading_view *view);
enum mh_error_code	mh_loading_view_ready(struct mh_loading_view *view,
	const struct mh_input_provider *inputs, struct mh_error *error_out);
enum mh_error_code	mh_loading_view_wait(struct mh_loading_view *view,
	enum mh_loading_action *action, struct mh_error *error_out);
void			mh_loading_view_close(struct mh_loading_view *view);

#endif
