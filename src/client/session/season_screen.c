#include <stdio.h>
#include <stdlib.h>

#include "client/diagnostics/options.h"
#include "client/session/season_screen.h"
#include "engine/core/error.h"
#include "engine/window/title.h"
#include "interfaces/input/input.h"
#include "interfaces/render/renderer.h"
#include "interfaces/window/window.h"

struct mh_season_screen {
	struct mh_error			error;
	char				diagnostic[256];
	const struct mh_window_system	*windows;
	const struct mh_input_provider	*inputs;
	struct mh_ui_list		draw_list;
	struct mh_renderer		*renderer;
	struct mh_window		*window;
	int				owns_window;
	const char			*capture_path;
	int				*accepted, *quit_requested;
	enum mh_app_status		status;
};

enum mh_error_code
mh_season_screen_open(const struct mh_window_system *windows,
	struct mh_window *window, const struct mh_input_provider *inputs,
	const struct mh_render_backend *backend,
	const struct mh_results_assets *results,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	const char *footer, const char *capture_path,
	const struct mh_av_settings *settings, int *accepted,
	int *quit_requested, const struct mh_debug_options *debug,
	struct mh_season_screen **output, struct mh_error *error_out)
{
	struct mh_season_screen *screen;
	unsigned int width, height;
	struct mh_window_system_options window_options;
	enum mh_error_code status;

	if (windows == NULL || inputs == NULL || accepted == NULL ||
		output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing season output");

	if (backend == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"season renderer is unavailable");

	width = MH_VIEW_WIDTH;
	height = MH_VIEW_HEIGHT;

	if (settings != NULL) {
		if (settings->width < MH_VIEW_MIN_WIDTH ||
			settings->width > MH_VIEW_MAX_WIDTH ||
			settings->height < MH_VIEW_MIN_HEIGHT ||
			settings->height > MH_VIEW_MAX_HEIGHT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid season viewport");

		width = (unsigned int)settings->width;
		height = (unsigned int)settings->height;
	}

	*accepted = 0;

	if (quit_requested != NULL)
		*quit_requested = 0;

	screen = (struct mh_season_screen *)calloc(1, sizeof(*screen));

	if (screen == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening season screen");

	if (!mh_results_season_draw(results, atlas, points, width, height,
		footer, &screen->draw_list)) {
		free(screen);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot draw season results");
	}

	screen->windows = windows;
	screen->inputs = inputs;
	window_options.offscreen = capture_path != NULL;
	window_options.preserve_environment = 0;
	status = mh_window_system_acquire(windows, &window_options, NULL);

	if (status != MH_ERROR_NONE) {
		free(screen);
		return mh_error_set(
			error_out, status, "cannot initialize season video");
	}

	screen->capture_path = capture_path;
	screen->accepted = accepted;
	screen->quit_requested = quit_requested;

	if (capture_path == NULL) {
		screen->window = window;
		screen->owns_window = window == NULL;

		if (window != NULL)
			status = mh_window_set_title(window,
				mh_window_title(screen->window, "Motorhead",
					"Motorhead season standings"),
				NULL);
		else
			status = mh_window_open(windows,
				debug != NULL && debug->window_titles
					? "Motorhead season standings"
					: "Motorhead",
				width, height,
				MH_WINDOW_RESIZABLE |
					(debug != NULL && debug->window_titles
							? MH_WINDOW_DIAGNOSTIC_TITLES
							: 0) |
					(settings != NULL && settings->fullscreen
							? MH_WINDOW_FULLSCREEN
							: 0),
				&screen->window, NULL);

		if (status != MH_ERROR_NONE) {
			free(screen);
			mh_window_system_release(windows);
			return mh_error_set(
				error_out, status, "cannot open season window");
		}
	}

	if (window != NULL && capture_path == NULL &&
		(settings == NULL || !settings->fullscreen)) {
		status = mh_window_set_size(window, width, height, NULL);

		if (status != MH_ERROR_NONE && status != MH_ERROR_UNSUPPORTED) {
			free(screen);
			mh_window_system_release(windows);
			return mh_error_set(error_out, status,
				"cannot resize season window");
		}
	}

	status = mh_renderer_open(backend, screen->window, NULL, 0, width,
		height,
		debug != NULL && debug->gpu ? MH_RENDERER_DIAGNOSTICS : 0,
		&screen->renderer, &screen->error);

	if (status != MH_ERROR_NONE)
		mh_error_copy(&screen->error, &screen->error,
			screen->diagnostic, sizeof(screen->diagnostic));
	else if (!mh_renderer_ui_upload(screen->renderer, atlas))
		status = mh_error_set(&screen->error, MH_ERROR_BACKEND,
			"cannot upload season artwork");

	screen->status =
		status == MH_ERROR_NONE ? MH_APP_RUNNING : MH_APP_FAILED;

	*output = screen;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_app_status
mh_season_screen_step(struct mh_season_screen *screen)
{
	struct mh_event event;
	int running;

	if (screen == NULL)
		return MH_APP_FAILED;

	if (screen->status != MH_APP_RUNNING)
		return screen->status;

	running = 1;

	while (mh_input_provider_event(screen->inputs, &event)) {
		if (event.type == MH_EVENT_QUIT ||
			event.type == MH_EVENT_WINDOW_CLOSE) {
			running = 0;

			if (screen->quit_requested != NULL)
				*screen->quit_requested = 1;
		}

		if (event.type == MH_EVENT_KEY_DOWN && !event.repeat) {
			if (event.shortcut == MH_SHORTCUT_ESCAPE) {
				*screen->accepted =
					screen->quit_requested != NULL;
				running = 0;
			}

			if (event.shortcut == MH_SHORTCUT_ENTER) {
				*screen->accepted = 1;
				running = 0;
			}
		}
	}

	if (!running) {
		if (screen->quit_requested != NULL && *screen->quit_requested)
			*screen->accepted = 0;

		screen->status = MH_APP_DONE;

		return screen->status;
	}

	if (!mh_renderer_ui_frame(screen->renderer, &screen->draw_list))
		screen->status = MH_APP_FAILED;
	else if (screen->capture_path != NULL)
		screen->status = mh_renderer_capture(
					screen->renderer, screen->capture_path)
			? MH_APP_DONE
			: MH_APP_FAILED;

	return screen->status;
}

int
mh_season_screen_close(struct mh_season_screen *screen)
{
	int success;

	if (screen == NULL)
		return 0;

	success = screen->status != MH_APP_FAILED;

	if (!success)
		fprintf(stderr, "Season screen: %s\n",
			screen->error.code != MH_ERROR_NONE
				? screen->error.message
				: "season presentation failed");

	if (screen->quit_requested != NULL && *screen->quit_requested)
		*screen->accepted = 0;

	mh_renderer_destroy(screen->renderer);

	if (screen->owns_window)
		mh_window_destroy(screen->window);

	mh_window_system_release(screen->windows);
	free(screen);

	return success;
}
