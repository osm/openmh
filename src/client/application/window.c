#include <stdlib.h>

#include "client/application/application_internal.h"
#include "client/menus/internal.h"
#include "engine/assets/icon.h"
#include "engine/window/title.h"
#include "interfaces/window/window.h"

enum {
	WINDOW_WIDTH = 960,
	WINDOW_HEIGHT = 600
};

enum mh_error_code
mh_application_open_window(struct mh_application *app, const char *title,
	struct mh_error *error_out)
{
	struct mh_window_system_options options;
	struct mh_image icon;
	unsigned int flags;
	enum mh_error_code status;

	title = mh_window_title(app->window, "Motorhead", title);

	if (app->window != NULL) {
		(void)mh_window_set_title(app->window, title, NULL);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!app->window_acquired) {
		options.offscreen = app->capture_path != NULL;
		options.preserve_environment = 1;
		status = mh_window_system_acquire(
			app->windows, &options, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		app->window_acquired = 1;
	}

	app->window_fullscreen = app->capture_path == NULL && app->fullscreen;
	flags = app->capture_path != NULL ? MH_WINDOW_HIDDEN
					: MH_WINDOW_RESIZABLE;

	if (app->window_fullscreen)
		flags |= MH_WINDOW_FULLSCREEN;

	status = mh_window_open(app->windows, title,
		app->capture_path != NULL ? MH_MENUS_WIDTH : WINDOW_WIDTH,
		app->capture_path != NULL ? MH_MENUS_HEIGHT : WINDOW_HEIGHT,
		flags |
			(app->session_options.debug.window_titles
					? MH_WINDOW_DIAGNOSTIC_TITLES
					: 0),
		&app->window, error_out);

	/*
	 * Missing icons never prevent an otherwise usable installation
	 * starting.
	 */
	if (status == MH_ERROR_NONE &&
		mh_icon_load_from(app->resources, app->asset_root, &icon,
			NULL) == MH_ERROR_NONE) {
		(void)mh_window_set_icon(app->window, &icon, NULL);
		free(icon.rgba);
	}

	return status;
}

void
mh_application_close_window(struct mh_application *app)
{
	mh_window_destroy(app->window);
	app->window = NULL;

	if (app->window_acquired)
		mh_window_system_release(app->windows);

	app->window_acquired = 0;
}
