#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/menus/internal.h"
#include "client/settings/availability.h"
#include "client/settings/render_choice.h"
#include "engine/core/error.h"
#include "engine/window/title.h"
#include "interfaces/window/window.h"

enum {
	RECORDING_SAVE_LAYOUT = 24,
	BACKGROUND_OBJECT_COUNT = 3
};

#define RECORDING_SAVE_IDLE_MS 1000000000UL

/* Borrowed menu/services stay alive until the owned activity closes. */
struct mh_menus_launch {
	struct mh_menus			*menus;
	struct mh_window		*window;
	const char			*asset_root, *disc_root;
	struct mh_settings		*settings;
	struct mh_settings_store	*store;
	struct mh_session_options	options;
	struct mh_settings		saved_settings;
	struct mh_activity		activity;
	unsigned int			menu_width, menu_height;
	int audio_mode, quit_requested, is_playback, league_champion,
		attract_manual_exit, session_started;
};

static enum mh_error_code
open_session(struct mh_menus_launch *launch, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;
	const char *asset_root, *disc_root;
	struct mh_settings *settings;
	const struct mh_session_options *options;
	struct mh_league_creation creation;
	struct mh_file recording;
	enum mh_demo_session_mode demo_mode;

	menus = launch->menus;
	asset_root = launch->asset_root;
	disc_root = launch->disc_root;
	settings = launch->settings;
	options = &launch->options;

	if (menus->launch_mode == MH_GAME_NETWORK) {
		status = mh_menus_multiplayer_open(menus, asset_root, settings,
			options, &launch->quit_requested, &launch->activity,
			error_out);
	} else if (menus->launch_mode == MH_GAME_ATTRACT) {
		status = mh_attract_session_open(asset_root, menus->profiles,
			&menus->horns, settings, options,
			menus->attract_previous, &menus->random_state,
			&launch->attract_manual_exit, &launch->quit_requested,
			&launch->activity, error_out);
	} else if (launch->is_playback) {
		recording.data = NULL;
		recording.size = 0;
		status = mh_recording_catalog_load(&menus->recordings,
			menus->recordings.menu.selected, &recording, error_out);

		demo_mode = MH_DEMO_SESSION_WATCH;

		if (menus->launch_mode == MH_GAME_GHOST)
			demo_mode = MH_DEMO_SESSION_GHOST;
		else if (menus->ghost.benchmark)
			demo_mode = MH_DEMO_SESSION_BENCHMARK;

		if (status == MH_ERROR_NONE)
			status = mh_demo_session_open_bytes(asset_root,
				&recording, menus->demo_name, demo_mode,
				&menus->horns, settings, options,
				&menus->random_state, &launch->quit_requested,
				&menus->take, &launch->activity, error_out);

		free(recording.data);
	} else if (menus->launch_mode == MH_GAME_LEAGUE) {
		status = mh_menus_league_creation(menus, &creation, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_league_session_open(asset_root, disc_root,
				menus->progress_directory, menus->unlock_cheat,
				menus->leagues, &creation, &menus->horns,
				settings, options, &menus->random_state,
				&launch->quit_requested,
				&launch->league_champion, &menus->take,
				&launch->activity, error_out);
	} else
		status = mh_single_session_open(asset_root, menus->profiles,
			&menus->horns, settings, options, &menus->random_state,
			&launch->quit_requested, &menus->take,
			&launch->activity, error_out);

	return status;
}

static enum mh_error_code
begin_launch(struct mh_menus_launch *launch, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;
	struct mh_settings *settings;
	struct mh_settings_store *store;

	menus = launch->menus;
	settings = launch->settings;
	store = launch->store;

	menus->launch_requested = 0;
	launch->options.record_class =
		(unsigned int)menus->track_unlocks.record_class;
	mh_menus_publish_settings(menus, settings);

	/*
	 * CFG stores the low two mode bits, including Demo returning as
	 * TimeAttack.
	 */
	if (menus->launch_mode != MH_GAME_NETWORK &&
		menus->launch_mode != MH_GAME_ATTRACT)
		settings->game_type = menus->launch_mode & 3;

	launch->is_playback = menus->launch_mode == MH_GAME_GHOST ||
		menus->launch_mode == MH_GAME_DEMO;
	launch->quit_requested = launch->league_champion =
		launch->attract_manual_exit = 0;
	status = mh_menus_controls_save(menus, error_out);

	/* Save all dirty Leagues on every frontend departure. */
	if (status == MH_ERROR_NONE && menus->leagues != NULL)
		status = mh_leagues_save(menus->leagues, error_out);

	if (status == MH_ERROR_NONE && store != NULL)
		status = mh_settings_store_save(store, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!settings->av.fullscreen) {
		status = mh_window_get_size(launch->window, &launch->menu_width,
			&launch->menu_height, error_out);

		if (status != MH_ERROR_NONE && status != MH_ERROR_UNSUPPORTED)
			return status;
	}

	launch->saved_settings = *settings;

	/*
	 * Save individual fields, then apply the preset. Custom retains its
	 * edits. Race assets must see the effective values.
	 */
	/* Attract applies this preset to its private runtime copy. */
	status = menus->launch_mode == MH_GAME_ATTRACT
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_av_settings_preset(&settings->av, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((menus->launch_mode == MH_GAME_ATTRACT ||
		(menus->launch_mode != MH_GAME_NETWORK &&
			settings->game_type == MH_GAME_QUICK_RACE)) &&
		menus->profiles == NULL) {
		status = mh_menus_profiles_load(menus, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (menus->launch_mode != MH_GAME_NETWORK) {
		mh_menu_audio_close(menus->audio);
		menus->audio = NULL;
	}

	launch->session_started = 1;

	return open_session(launch, error_out);
}

static enum mh_error_code
refresh_menu_preferences(struct mh_menus *menus,
	const struct mh_settings *settings, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	menus->single.mode = settings->game_type;
	mh_menus_restrict_mode(menus);

	for (i = 0; i < menus->car_catalog.count; i++)
		if (menus->car_numbers[i] == settings->car + 1)
			menus->single.car = i;

	for (i = 0; i < menus->track_catalog.count; i++)
		if (menus->track_numbers[i] == settings->track + 1)
			menus->details.track = i;

	menus->details.laps = settings->laps;
	menus->details.automatic = settings->automatic;
	menus->details.difficulty = settings->difficulty;
	menus->race.catch_up = settings->catch_up;
	menus->car.record = settings->record_race;
	menus->car.horn = settings->horn;
	menus->navigation_keys.confirmation = settings->menu_confirmation;
	strcpy(menus->demo_name, settings->demo_filename);
	status = mh_menus_controls_reload(menus, error_out);

	if (status == MH_ERROR_NONE) {
		mh_graphics_catalog_close(menus->graphics_catalog);
		menus->graphics_catalog = NULL;
		menus->graphics.device = settings->renderer;
		status = mh_graphics_catalog_open_from(menus->windows,
			settings->av.width, settings->av.height,
			mh_render_choice_modes(), MH_RENDER_CHOICE_COUNT,
			menus->graphics.device,
			mh_menus_feature_enabled(&menus->configuration,
				MH_MENU_FEATURE_PRESENTATION),
			mh_menus_feature_enabled(&menus->configuration,
				MH_MENU_FEATURE_SCREENSHOTS),
			&menus->graphics_catalog, &menus->graphics.mode,
			error_out);
	}

	/*
	 * Reload ADPs, including their shared-RNG initialization, before
	 * reloading LGFs and restoring CFG LeagueName.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_menus_profiles_load(menus, error_out);

	if (status == MH_ERROR_NONE &&
		mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_LEAGUE))
		status = mh_menus_league_reload(menus, error_out);

	menus->unlock_cheat = menus->unlock_all ||
		mh_cheat_unlock_tracks(
			settings->player_name, settings->team_name);

	if (status == MH_ERROR_NONE)
		status = mh_menus_unlocks_refresh(menus, error_out);

	return status;
}

static enum mh_error_code
restore_menu_page(
	struct mh_menus_launch *launch, int failed, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;
	const struct mh_menu_hooks *hooks;
	unsigned int i;

	menus = launch->menus;

	if (mh_menus_compact(menus)) {
		mh_menus_compact_bind(menus);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/*
	 * Return League to standings unless Champion (4), and mode 4 to Multi.
	 * Rebuild controllers while preserving selections/shared RNG.
	 */
	hooks = menus->menu.hooks;
	status = mh_menus_recordings_reload(
		launch->asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	menus->main_page.timer = menus->single_page.timer =
		menus->race_page.timer = menus->car_page.timer = 0;
	menus->multi_page.timer = 0;
	menus->launch_page.timer = menus->attract_page.timer =
		menus->exit_page.timer = menus->ghost_page.timer =
			menus->save_page.timer = 0;

	for (i = 0; i < MH_MENUS_LEAGUE_COUNT; i++)
		menus->league_page[i].timer = 0;

	memset(&menus->modal, 0, sizeof(menus->modal));
	menus->dialog_kind = menus->main.exit_answer = 0;
	mh_menu_init(&menus->menu, &menus->main_page, hooks, menus);
	mh_menu_restore_page(&menus->menu,
		&menus->league_page[MH_MENUS_LEAGUE_STANDINGS],
		&menus->multi_page, 1, menus->launch_mode,
		launch->league_champion ? MH_LEAGUE_CHAMPION : 0);

	if (!mh_menus_page_enabled(menus, menus->menu.current)) {
		menus->menu.current = &menus->main_page;
		menus->menu.layout = menus->main_page.layout;
	}

	/*
	 * Reload NPCs after page binding on every frontend entry, including
	 * returns from local races and attract playback.
	 */
	if (mh_menus_feature_enabled(
		&menus->configuration, MH_MENU_FEATURE_NETWORK))
		status =
			mh_menus_multiplayer_addresses_reload(menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	menus->menu.idle_ms = mh_attract_idle_delay(
		menus->menu.current == &menus->main_page, menus->launch_mode,
		launch->attract_manual_exit || failed);

	if (menus->menu.current ==
		&menus->league_page[MH_MENUS_LEAGUE_STANDINGS]) {
		status = mh_menus_league_sync(menus, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (menus->take != NULL &&
		!mh_menus_page_enabled(menus, &menus->save_page)) {
		mh_demo_file_abort(menus->take);
		menus->take = NULL;
	}

	if (menus->take != NULL) {
		menus->save_return = menus->menu.current;
		menus->menu.current = &menus->save_page;
		menus->menu.layout = RECORDING_SAVE_LAYOUT;
		menus->menu.idle_ms = RECORDING_SAVE_IDLE_MS;
		status = mh_menu_recording_begin(&menus->saving, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
restore_launch_settings(
	struct mh_menus_launch *launch, struct mh_error *error_out)
{
	struct mh_menus *menus;
	struct mh_settings *settings;
	struct mh_settings snapshot;
	enum mh_error_code status;

	menus = launch->menus;
	settings = launch->settings;
	status = MH_ERROR_NONE;

	/*
	 * Reload CFG before restoring only five race options. Keep the unsaved
	 * runtime preset and recording failure flag out of that file. Mode 4
	 * additionally restores its returned lobby profile.
	 */
	snapshot = *settings;

	if (launch->store == NULL)
		/* Explicit no-settings diagnostic. */
		*settings = launch->saved_settings;
	else
		status = mh_settings_store_reload(
			launch->store, &menus->random_state, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_settings_restore_menu(settings, &snapshot,
			menus->launch_mode, settings, error_out);

	if (status == MH_ERROR_NONE)
		mh_settings_apply_availability(settings);

	return status;
}

static enum mh_error_code
resume_menu_presentation(
	struct mh_menus_launch *launch, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = launch->menus;

	memset(menus->work.pixels, 0, menus->work.size);
	memset(menus->present.pixels, 0, menus->present.size);
	status = mh_menu_background_reset(
		&menus->background, BACKGROUND_OBJECT_COUNT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menus_open_audio(menus, launch->asset_root,
			launch->options.music_root, launch->audio_mode,
			error_out);

	if (status == MH_ERROR_NONE && launch->menu_width != 0) {
		status = mh_window_set_size(launch->window, launch->menu_width,
			launch->menu_height, error_out);

		if (status == MH_ERROR_UNSUPPORTED)
			status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status == MH_ERROR_NONE)
		status = mh_window_set_title(launch->window,
			mh_window_title(launch->window, "Motorhead",
				"Motorhead main-menu preview"),
			error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_window_request_raise(launch->window, error_out);

		/* A compositor may not offer application-controlled focus. */
		if (status == MH_ERROR_UNSUPPORTED)
			status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return status;
}

/* Copy before closing the activity can invalidate its diagnostic buffer. */
static void
retain_launch_failure(struct mh_menus_launch *launch, struct mh_error *failure,
	unsigned char *text, size_t capacity)
{
	struct mh_menus *menus;
	size_t length;

	menus = launch->menus;
	text[0] = 0;
	*failure = mh_success;

	if (menus->error.code == MH_ERROR_NONE)
		return;

	fprintf(stderr, "%s: %s\n", launch->is_playback ? "Playback" : "Race",
		menus->error.message);
	length = strlen(menus->error.message);

	if (length > capacity - 2)
		length = capacity - 2;

	memcpy(text, menus->error.message, length);
	text[length] = text[length + 1] = 0;
	mh_error_set(failure, menus->error.code, (const char *)text);
	menus->error = mh_success;
}

static void
report_menu_resume(const struct mh_menus *menus)
{
	const char *message;

	if (menus->take != NULL)
		message = "Frontend resumed: recording save";
	else if (menus->menu.current ==
		&menus->league_page[MH_MENUS_LEAGUE_STANDINGS])
		message = "Frontend resumed: standings";
	else if (menus->menu.current == &menus->multi_page)
		message = "Frontend resumed: multiplayer";
	else
		message = "Frontend resumed: main";

	puts(message);
	fflush(stdout);
}

static void
finish_launch(struct mh_menus_launch *launch)
{
	struct mh_menus *menus;
	struct mh_error failure;
	unsigned char failure_text[256];
	enum mh_error_code status;

	menus = launch->menus;

	if (menus->launch_mode == MH_GAME_NETWORK) {
		mh_menu_audio_close(menus->audio);
		menus->audio = NULL;
	}

	retain_launch_failure(
		launch, &failure, failure_text, sizeof(failure_text));
	mh_activity_close(&launch->activity);

	if (restore_launch_settings(launch, &menus->error) != MH_ERROR_NONE)
		return;

	status = refresh_menu_preferences(
		menus, launch->settings, &menus->error);

	if (launch->quit_requested)
		menus->exit_requested = 1;

	if (status != MH_ERROR_NONE || launch->quit_requested)
		return;

	if (restore_menu_page(launch, failure_text[0] != 0, &menus->error) !=
		MH_ERROR_NONE)
		return;

	status = resume_menu_presentation(launch, &menus->error);

	if (status == MH_ERROR_NONE && failure_text[0] != 0)
		mh_menus_show_error(menus, failure);

	if (menus->error.code == MH_ERROR_NONE)
		report_menu_resume(menus);
}

void
mh_menus_request_launch(struct mh_menus *menus)
{
	static const unsigned char title[] =
		"This mode is not connected yet.\0";
	struct mh_error error;

	menus->launch_mode = menus->menu.current == &menus->ghost_page
		? menus->ghost.mode
		: menus->single.mode;

	if (!mh_menus_mode_enabled(menus, menus->launch_mode)) {
		mh_menus_sound_event(menus, MH_MENU_SOUND_LIMIT);
		return;
	}

	if (menus->launch_mode == MH_GAME_LEAGUE) {
		if (mh_menus_league_prepare(menus, &error) != MH_ERROR_NONE) {
			mh_menus_show_error(menus, error);
			return;
		}
	}

	if (menus->launch_mode <= MH_GAME_GHOST ||
		menus->launch_mode == MH_GAME_DEMO) {
		mh_menus_request(&menus->menu, &menus->launch_page);
		return;
	}

	mh_menus_show_notice(menus, title, sizeof(title));
}

void
mh_menus_launch_close(struct mh_menus *menus)
{
	if (menus->launch == NULL)
		return;

	mh_activity_close(&menus->launch->activity);
	free(menus->launch);
	menus->launch = NULL;
}

void
mh_menus_launch_open(struct mh_menus *menus, struct mh_window *window,
	const char *asset_root, const char *disc_root,
	struct mh_settings *settings, struct mh_settings_store *store,
	const struct mh_session_options *options, int audio_mode)
{
	struct mh_menus_launch *launch;

	if (menus->launch != NULL) {
		mh_error_set(&menus->error, MH_ERROR_STATE,
			"frontend launch is already open");
		return;
	}

	launch = (struct mh_menus_launch *)calloc(1, sizeof(*launch));

	if (launch == NULL) {
		mh_error_set(&menus->error, MH_ERROR_MEMORY,
			"out of memory preparing frontend launch");
		return;
	}

	launch->menus = menus;
	launch->window = window;
	launch->asset_root = asset_root;
	launch->disc_root = disc_root;
	launch->settings = settings;
	launch->store = store;
	launch->options = *options;
	launch->options.window = window;
	launch->audio_mode = audio_mode;
	mh_activity_init(&launch->activity);
	menus->launch = launch;
	(void)begin_launch(launch, &menus->error);

	if (!launch->session_started)
		mh_menus_launch_close(menus);
}

enum mh_app_status
mh_menus_launch_step(struct mh_menus *menus, unsigned long *delay_ms)
{
	struct mh_menus_launch *launch;
	enum mh_app_status status;

	*delay_ms = 0;
	launch = menus->launch;

	if (launch == NULL)
		return menus->error.code == MH_ERROR_NONE ? MH_APP_DONE
							: MH_APP_FAILED;

	if (mh_activity_is_open(&launch->activity)) {
		status = mh_activity_step(&launch->activity);
		*delay_ms = mh_activity_delay(&launch->activity);

		if (status == MH_APP_RUNNING)
			return MH_APP_RUNNING;

		(void)mh_activity_error(&launch->activity, &menus->error);
	}

	finish_launch(launch);
	mh_menus_launch_close(menus);

	return menus->error.code == MH_ERROR_NONE ? MH_APP_DONE : MH_APP_FAILED;
}
