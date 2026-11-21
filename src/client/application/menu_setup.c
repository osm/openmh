#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/menus/internal.h"
#include "client/settings/render_choice.h"
#include "engine/audio/horn.h"
#include "engine/core/error.h"
#include "interfaces/input/force.h"
#include "interfaces/render/canvas.h"
#include "interfaces/storage/directory.h"
#include "interfaces/window/window.h"

#define MENU_ROW_NOISE_SEED 741118UL

static void
select_initial_rows(struct mh_application *app)
{
	struct mh_menus *menus;

	menus = app->menus;

	menus->main.selected = app->start_page == MH_MENUS_PAGE_MAIN
		? (unsigned int)app->start_selection
		: 0;
	menus->single.selected = app->start_page == MH_MENUS_PAGE_SINGLE
		? (unsigned int)app->start_selection
		: 0;
	menus->race.selected = app->start_page == MH_MENUS_PAGE_SETUP
		? (unsigned int)app->start_selection
		: 0;
	menus->car.selected = app->start_page == MH_MENUS_PAGE_CAR
		? (unsigned int)app->start_selection
		: 0;
	menus->ghost_initial_focus = app->start_page == MH_MENUS_PAGE_GHOST
		? (unsigned int)app->start_selection
		: 0;
	menus->league.choice = app->start_page >= MH_MENUS_PAGE_LEAGUE &&
			app->start_page <= MH_MENUS_PAGE_LEAGUE_DELETE
		? (unsigned int)app->start_selection
		: 0;
	menus->options.selected = app->start_page == MH_MENUS_PAGE_OPTIONS
		? (unsigned int)app->start_selection
		: 0;
	menus->sound.selected = app->start_page == MH_MENUS_PAGE_SOUND
		? (unsigned int)app->start_selection
		: 0;
	menus->graphics.selected = app->start_page == MH_MENUS_PAGE_GRAPHICS
		? (unsigned int)app->start_selection
		: 0;
	menus->personal.selected = app->start_page == MH_MENUS_PAGE_PERSONAL
		? (unsigned int)app->start_selection
		: 0;
	menus->controls.row = app->start_page == MH_MENUS_PAGE_CONTROLS
		? (unsigned int)app->start_selection
		: 0;
	mh_menu_rankings_defaults(&menus->rankings);

	if (app->start_page == MH_MENUS_PAGE_RANKINGS)
		menus->rankings.focus = (unsigned int)app->start_selection;
}

static void
apply_settings(struct mh_application *app)
{
	struct mh_menus *menus;
	struct mh_settings *settings;

	menus = app->menus;
	settings = app->preferences == NULL ? &app->defaults : app->preferences;

	menus->unlock_all = app->unlock_all;
	menus->unlock_cheat = menus->unlock_all ||
		mh_cheat_unlock_tracks(
			settings->player_name, settings->team_name);
	menus->car.record = (unsigned int)app->record_race;
	menus->race.catch_up = (unsigned int)app->catch_up;
	menus->single.mode = (unsigned int)app->game_type;
	menus->single.car = (unsigned int)app->car_number - 1;
	menus->details.track = (unsigned int)app->track_number - 1;
	menus->navigation_keys.confirmation =
		(unsigned int)app->menu_confirmation;
	strcpy(menus->demo_name, settings->demo_filename);
	menus->edition = app->content_edition;
	menus->asset_root = app->asset_root;
	menus->resources = app->resources;
	menus->debug = app->session_options.debug;
	menus->font_source = app->session_options.font_source;
	menus->wireframe_source = app->session_options.wireframe_source;
	menus->catalog_source = app->session_options.catalog_source;
	menus->profile_source = app->session_options.profile_source;
	menus->palette_source = app->session_options.palette_source;
	menus->layout_source = app->session_options.layout_source;
	menus->horn_source = app->session_options.horn_source;
	menus->sprite_source = app->session_options.sprite_source;
	menus->image_decoder = app->session_options.image_decoder;
	menus->thumbnail_decoder = app->session_options.thumbnail_decoder;
	menus->user_data = app->user_data;
	menus->settings = settings;

	if (app->renderer < MH_RENDER_CHOICE_COUNT)
		menus->settings->renderer = app->renderer;
}

static enum mh_error_code
load_menu_configuration(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;

	apply_settings(app);
	menus->configuration = app->menu_configuration;
	mh_menus_configuration_resolve(&menus->configuration, menus->edition,
		menus->network != NULL,
		menus->audio_provider != NULL && menus->audio_decoder != NULL,
		mh_input_has_force(menus->inputs));
	mh_menus_configuration_bind(menus);
	mh_menus_restrict_mode(menus);

	status = mh_menus_controls_load(
		menus, app->controls_directory, !app->no_settings, error_out);

	if (status == MH_ERROR_NONE &&
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_NETWORK))
		status = mh_menus_multiplayer_addresses_load(menus,
			app->addresses_directory, !app->no_settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_menus_compact(menus)) {
		menus->single.mode = MH_GAME_QUICK_RACE;
		menus->car.record = 0;
	}

	menus->graphics.device = menus->settings->renderer;

	return mh_graphics_catalog_open_from(app->windows,
		menus->settings->av.width, menus->settings->av.height,
		mh_render_choice_modes(), MH_RENDER_CHOICE_COUNT,
		menus->graphics.device,
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_PRESENTATION),
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_SCREENSHOTS),
		&menus->graphics_catalog, &menus->graphics.mode, error_out);
}

static enum mh_error_code
load_menu_assets(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;
	status = MH_ERROR_NONE;

	if (app->capture_path == NULL) {
		status = mh_progress_path_from(app->user_data,
			app->progress_directory, menus->progress_path,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		app->progress_directory = menus->progress_path;
	}

	menus->progress_directory = app->progress_directory;

	if (app->capture_path == NULL ||
		app->session_options.recording_directory != NULL)
		status = mh_recording_directory_from(app->user_data,
			app->session_options.recording_directory,
			menus->recording_directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menus_resources_load(
		app->asset_root, app->progress_directory, menus, error_out);
}

static enum mh_error_code
configure_race_choices(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;

	menus = app->menus;
	app->session_options.course_music_count = menus->track_catalog.count;

	menus->special_cars = menus->unlock_all ||
		(app->special_cars_override == 2
				? mh_menus_special_cars_available(
					app->resources, app->asset_root)
				: (int)app->special_cars_override);

	menus->details.laps = (unsigned int)app->laps;
	menus->details.automatic = (unsigned int)app->automatic;
	menus->details.stage = (unsigned int)app->stage;
	menus->details.difficulty = menus->settings->difficulty;

	if (app->preferences != NULL)
		menus->car.horn = app->preferences->horn;

	if (app->horn_name != NULL)
		return mh_horn_find(&menus->horns, app->horn_name,
			&menus->car.horn, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_menu_devices(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;
	status = mh_application_open_window(
		app, "Motorhead loading frontend", error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_canvas_open(app->canvases, app->window, MH_MENUS_WIDTH,
		MH_MENUS_HEIGHT, &app->canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_menus_bind_pages(menus, app->start_page);

	status = mh_keyboard_init(&app->keyboard, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_input_open_from(app->inputs, &app->devices,
		app->capture_path == NULL ? app->window : NULL,
		app->window_fullscreen || menus->settings->av.grab_mouse,
		error_out);
}

static int
menu_audio_mode(const struct mh_application *app)
{
	if (app->audio_mode == MH_MENUS_AUDIO_REQUIRED)
		return app->audio_mode;

	if (app->audio_mode == MH_MENUS_AUDIO_AUTO && app->capture_path == NULL)
		return app->audio_mode;

	return MH_MENUS_AUDIO_DISABLED;
}

static void
initialize_menu_state(struct mh_application *app, struct mh_menus *menus)
{
	app->menus = menus;
	menus->windows = app->windows;
	menus->inputs = app->inputs;
	menus->audio_provider = app->audio_provider;
	menus->audio_decoder = app->audio_decoder;
	menus->timer = &app->timer;
	menus->random_state = app->settings_seed;
	menus->row_noise = MENU_ROW_NOISE_SEED;
	menus->network = app->session_options.network;
	menus->network_wire = app->session_options.network_wire;
	menus->network_port = MH_NET_LANINET_PORT;
	select_initial_rows(app);

	menus->rankings_directory = app->session_options.no_records
		? NULL
		: app->session_options.record_directory;
	menus->music_root = app->session_options.music_root;
	app->session_options.course_music = menus->source_music;
	app->session_options.controls = &menus->applied_controls;
	menus->league_directory = app->league_directory;
	menus->capture_only = app->capture_path != NULL;
	menus->car_scene.catalog.unlocked = (unsigned int)app->unlocked_cars;
	menus->car_override = app->unlocked_cars != 0;
	menus->track_override = app->unlocked_tracks != 0;
	menus->race_scene.unlocked = (unsigned int)app->unlocked_tracks;
	menus->startup_dialog = app->startup_dialog;
	app->canvas = NULL;
	app->success = 0;
	memset(&app->devices, 0, sizeof(app->devices));
	menus->input_mode = 1;
}

enum mh_app_status
mh_application_open_menus(struct mh_application *app)
{
	struct mh_menus *menus;
	struct mh_window_system_options window_options;
	enum mh_error_code status;

	menus = (struct mh_menus *)calloc(1, sizeof(*menus));

	if (menus == NULL) {
		fprintf(stderr, "Menu preview: out of memory\n");
		return mh_application_stop(app, 1);
	}

	initialize_menu_state(app, menus);

	if (!app->window_acquired) {
		window_options.offscreen = app->capture_path != NULL;
		window_options.preserve_environment = 1;
		status = mh_window_system_acquire(
			app->windows, &window_options, &menus->error);

		if (status != MH_ERROR_NONE)
			return mh_application_stop(app, 1);

		app->window_acquired = 1;
	}

	if (!app->session_options.no_records &&
		app->session_options.record_directory == NULL &&
		app->capture_path == NULL) {
		status = mh_user_data_directory(app->user_data,
			&menus->rankings_preferred, &menus->error);

		if (status != MH_ERROR_NONE)
			return mh_application_stop(app, 1);

		menus->rankings_directory = menus->rankings_preferred;
	}

	if (load_menu_configuration(app, &menus->error) != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	if (load_menu_assets(app, &menus->error) != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	if (configure_race_choices(app, &menus->error) != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	/*
	 * The original loads ADPs/Leagues on every interactive frontend entry,
	 * even when the selected mode is not League. Captures remain explicit.
	 */
	if (mh_menus_feature_enabled(
		&menus->configuration, MH_MENU_FEATURE_LEAGUE) &&
		(app->capture_path == NULL ||
			(app->start_page >= MH_MENUS_PAGE_LEAGUE &&
				app->start_page <=
					MH_MENUS_PAGE_LEAGUE_DELETE) ||
			(menus->single.mode == 1 &&
				app->league_directory != NULL))) {
		status = mh_menus_league_prepare(menus, &menus->error);

		if (status != MH_ERROR_NONE)
			return mh_application_stop(app, 1);
	}

	status = mh_menus_open_audio(menus, app->asset_root,
		app->session_options.music_root, menu_audio_mode(app),
		&menus->error);

	if (status != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	if (open_menu_devices(app, &menus->error) != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	/*
	 * Deterministic capture/finite inspection runs do not launch sessions.
	 */
	app->idle_enabled = mh_menus_feature_enabled(&menus->configuration,
				MH_MENU_FEATURE_ATTRACT) &&
		app->capture_path == NULL && app->frame_limit == 0;
	menus->menu.idle_ms = mh_attract_idle_delay(
		menus->menu.current == &menus->main_page, 0, 0);

	if (app->idle_enabled)
		status = mh_game_timer_real_word(
			menus->timer, &menus->idle_started, &menus->error);

	if (status != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	memset(app->down, 0, sizeof(app->down));
	app->running = 1;

	app->frame_index = 0;
	app->frame_started = 0;

	app->phase = APPLICATION_MENU;

	return MH_APP_RUNNING;
}
