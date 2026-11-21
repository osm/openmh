#ifndef MH_CLIENT_SESSION_SEASON_SCREEN_H
#define MH_CLIENT_SESSION_SEASON_SCREEN_H

#include "client/application/status.h"
#include "engine/core/error.h"
#include "engine/settings/av.h"
#include "engine/ui/results.h"

struct mh_debug_options;

struct mh_season_screen;
struct mh_window_system;
struct mh_window;
struct mh_input_provider;
struct mh_render_backend;

/*
 * Window system, input provider, renderer descriptor and output pointers are
 * borrowed until close. A non-NULL capture path renders one offscreen frame.
 * An optional client window is borrowed without any active renderer; NULL
 * creates a standalone window. Offscreen capture never uses the client window.
 * Enter accepts; Escape accepts only when quit_requested is supplied, and
 * otherwise cancels. Window close sets a supplied quit_requested flag and
 * overrides acceptance in that case. Opening clears result flags before
 * allocation.
 */
enum mh_error_code	mh_season_screen_open(
	const struct mh_window_system *windows, struct mh_window *window,
	const struct mh_input_provider *inputs,
	const struct mh_render_backend *backend,
	const struct mh_results_assets *results,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	const char *footer, const char *capture_path,
	const struct mh_av_settings *settings, int *accepted,
	int *quit_requested, const struct mh_debug_options *debug,
	struct mh_season_screen **output, struct mh_error *error_out);
enum mh_app_status	mh_season_screen_step(struct mh_season_screen *screen);
int			mh_season_screen_close(struct mh_season_screen *screen);

#endif
