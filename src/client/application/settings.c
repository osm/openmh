#include <string.h>

#include "client/application/application_internal.h"
#include "client/settings/availability.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_profile.h"

static void
merge_saved_settings(struct mh_application *app)
{
	if (!(app->overrides & MH_APP_OVERRIDE_CAR))
		app->car_number = app->preferences->car + 1;

	if (!(app->overrides & MH_APP_OVERRIDE_TRACK))
		app->track_number = app->preferences->track + 1;

	if (!(app->overrides & MH_APP_OVERRIDE_MODE))
		app->game_type = app->preferences->game_type;

	if (!(app->overrides & MH_APP_OVERRIDE_LAPS))
		app->laps = app->preferences->laps;

	if (!(app->overrides & MH_APP_OVERRIDE_CATCH_UP))
		app->catch_up = app->preferences->catch_up;

	if (!(app->overrides & MH_APP_OVERRIDE_AUTOMATIC))
		app->automatic = app->preferences->automatic;

	if (!(app->overrides & MH_APP_OVERRIDE_RECORD))
		app->record_race = app->preferences->record_race;

	if (!(app->overrides & MH_APP_OVERRIDE_CONFIRMATION))
		app->menu_confirmation = app->preferences->menu_confirmation;

	if (app->overrides & MH_APP_OVERRIDE_FULLSCREEN)
		app->preferences->av.fullscreen = app->fullscreen;
	else
		app->fullscreen = app->preferences->av.fullscreen;

	if (app->overrides & MH_APP_OVERRIDE_SHOW_FPS)
		app->preferences->av.show_fps = app->show_fps;

	if (app->overrides & MH_APP_OVERRIDE_GRAB_MOUSE)
		app->preferences->av.grab_mouse = app->grab_mouse;

	if (app->overrides & MH_APP_OVERRIDE_VSYNC)
		app->preferences->av.vsync = app->vsync;

	if (app->overrides & MH_APP_OVERRIDE_MAX_FPS)
		app->preferences->av.max_fps = app->max_fps;

	if (app->player_name != NULL)
		strcpy(app->preferences->player_name, app->player_name);

	if (app->team_name != NULL)
		strcpy(app->preferences->team_name, app->team_name);
}

enum mh_error_code
mh_application_load_settings(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_settings_names renderers;
	enum mh_error_code status;

	app->settings_seed = app->initial_seed;

	status = mh_settings_init(&app->defaults,
		app->player_name == NULL ? "Player" : app->player_name, 255,
		255, mh_render_choice_default(), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	app->defaults.av.race_audio = mh_race_audio_default();

	if (app->team_name != NULL)
		strcpy(app->defaults.team_name, app->team_name);

	/* Start at 1280x1024 if the default renderer supports that width. */
	if (mh_render_choice_modes()[app->defaults.renderer].max_width >=
		1280) {
		app->defaults.av.width = 1280;
		app->defaults.av.height = 1024;
	}

	app->defaults.av.fullscreen = app->fullscreen;
	app->defaults.av.show_fps = app->show_fps;
	app->defaults.av.vsync = app->vsync;
	app->defaults.av.max_fps = app->max_fps;
	app->defaults.av.grab_mouse = app->grab_mouse;
	app->defaults.car = (unsigned int)app->car_number - 1;
	app->defaults.track = (unsigned int)app->track_number - 1;
	app->defaults.game_type = (unsigned int)app->game_type;
	app->defaults.laps = (unsigned int)app->laps;
	app->defaults.catch_up = (unsigned int)app->catch_up;
	app->defaults.automatic = (unsigned int)app->automatic;
	app->defaults.record_race = (unsigned int)app->record_race;
	app->defaults.menu_confirmation = (unsigned int)app->menu_confirmation;

	if (!app->no_settings &&
		(app->settings_path != NULL || app->capture_path == NULL)) {
		renderers.names = mh_render_choice_names();
		renderers.count = MH_RENDER_CHOICE_COUNT;
		status = mh_settings_store_open_with_sources(
			app->session_options.horn_source,
			app->session_options.catalog_source, app->resources,
			app->user_data, &renderers, app->asset_root,
			app->settings_path, &app->defaults, &app->settings_seed,
			&app->settings_store, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		app->preferences =
			mh_settings_store_values(app->settings_store);

		merge_saved_settings(app);
	}

	if (app->preferences == NULL)
		app->preferences = &app->defaults;

	mh_settings_apply_availability(app->preferences);

	if (app->renderer < MH_RENDER_CHOICE_COUNT &&
		!mh_render_choice_available(app->renderer))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"requested renderer is not included in this build");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
