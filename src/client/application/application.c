#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/application/opening.h"
#include "client/application/race_frontend_internal.h"
#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/storage/storage.h"
#include "engine/storage/user_root.h"
#include "interfaces/input/input.h"
#include "interfaces/render/canvas.h"
#include "interfaces/render/service.h"
#include "interfaces/window/window.h"

/* Visual/session histories still have one active application owner. */
static struct mh_application *active_application;

enum mh_app_status
mh_application_stop(struct mh_application *app, int exit_code)
{
	app->exit_code = exit_code;
	app->status = exit_code == 0 ? MH_APP_DONE : MH_APP_FAILED;

	if (app->menus != NULL && app->menus->error.code != MH_ERROR_NONE)
		mh_error_copy(&app->menus->error, &app->menus->error,
			app->error, sizeof(app->error));

	return app->status;
}

enum mh_error_code
mh_application_save_menus(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;

	if (app->capture_path != NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_menus_controls_save(menus, error_out);

	if (status == MH_ERROR_NONE && menus->leagues != NULL &&
		mh_menus_league_page_index(menus) != MH_MENUS_LEAGUE_NEW)
		status = mh_leagues_save(menus->leagues, error_out);

	if (status == MH_ERROR_NONE && app->settings_store != NULL) {
		mh_menus_publish_settings(menus, app->preferences);
		status = mh_settings_store_save(app->settings_store, error_out);
	}

	return status;
}

static void
finish_menus(struct mh_application *app)
{
	struct mh_menus *menus;

	menus = app->menus;

	if (app->success && menus->error.code == MH_ERROR_NONE)
		app->success = mh_application_save_menus(app, &menus->error) ==
			MH_ERROR_NONE;

	if (menus->error.code != MH_ERROR_NONE)
		app->success = 0;

	mh_application_stop(app, !app->success);
}

static int
close_menus(struct mh_application *app)
{
	struct mh_menus *menus;

	menus = app->menus;

	if (menus->error.code != MH_ERROR_NONE) {
		fprintf(stderr, "Menu preview: %s\n", menus->error.message);
		app->success = 0;
	}

	if (app->success)
		mh_menus_print_summary(menus, app->preferences);

	mh_menus_launch_close(menus);
	mh_input_close(&app->devices);
	mh_canvas_destroy(app->canvas);
	mh_menus_destroy(menus);

	return !app->success;
}

static void
initialize_services(
	struct mh_application *app, const struct mh_app_services *services)
{
	app->capture = services->rendering->capture;

	if (services->game_directory != NULL)
		strcpy(app->game_directory, services->game_directory);

	app->renderers = services->rendering->renderers;
	app->canvases = services->rendering->canvases;
	app->windows = services->rendering->windows;
	app->inputs = services->inputs;
	app->audio_provider = services->audio;
	app->audio_decoder = services->audio_decoder;
	app->storage = services->storage;
	app->resources = services->resources;
	app->resource_archives = services->resource_archives;
	app->user_data = services->user_data;
	(void)mh_game_timer_init(&app->timer,
		services->lifecycle == NULL ? services->clock
					: &app->clock.source,
		NULL);
	mh_activity_init(&app->opening);
	app->status = MH_APP_RUNNING;
	app->exit_code = 1;
}

static void
initialize_session_services(
	struct mh_application *app, const struct mh_app_services *services)
{
	/*
	 * Every session inherits the process-wide mute without changing saves.
	 */
	if (app->audio_mode == MH_MENUS_AUDIO_DISABLED)
		app->audio_provider = NULL;

	app->session_options.image_source = services->image_source;

	app->session_options.font_source = services->font_source;
	app->session_options.wireframe_source = services->wireframe_source;
	app->session_options.sprite_source = services->sprite_source;

	app->session_options.model_decoder = services->model_decoder;
	app->session_options.world_decoder = services->world_decoder;
	app->session_options.atlas_source = services->atlas_source;
	app->session_options.animation_source = services->animation_source;
	app->session_options.navigation_source = services->navigation_source;
	app->session_options.catalog_source = services->catalog_source;
	app->session_options.profile_source = services->profile_source;
	app->session_options.palette_source = services->palette_source;
	app->session_options.layout_source = services->layout_source;
	app->session_options.material_source = services->material_source;
	app->session_options.env_sound_source = services->env_sound_source;
	app->session_options.horn_source = services->horn_source;
	app->session_options.collision_source = services->collision_source;
	app->session_options.image_decoder = services->image_decoder;
	app->session_options.thumbnail_decoder = services->thumbnail_decoder;

	app->session_options.portrait_decoder = services->portrait_decoder;

	app->session_options.capture = services->rendering->capture;

	app->session_options.wait_for_loading = 1;
	app->session_options.screenshot_data = services->screenshot_data;
	memcpy(app->screenshot_encoders, services->screenshot_encoders,
		sizeof(app->screenshot_encoders));
	app->session_options.renderers = app->renderers;
	app->session_options.canvases = app->canvases;
	app->session_options.windows = app->windows;
	app->session_options.inputs = app->inputs;
	app->session_options.audio_provider = app->audio_provider;
	app->session_options.retro_source = services->retro_source;
	app->session_options.audio_decoder = app->audio_decoder;
	app->session_options.audio_trace = services->audio_trace |
		mh_debug_audio_trace(&app->session_options.debug);
	app->session_options.videos = services->videos;
	app->session_options.network = app->session_options.network_legacy &&
			services->legacy_network != NULL
		? services->legacy_network
		: services->network;
	app->session_options.network_trace = services->network_trace ||
		app->session_options.debug.network_events;
	app->session_options.clipboard = services->clipboard;
	app->session_options.timer = &app->timer;
	app->session_options.resources = app->resources;
	app->session_options.user_data = app->user_data;
	app->session_options.screenshot_directory =
		app->session_options.debug.screenshot_directory != NULL
		? app->session_options.debug.screenshot_directory
		: services->screenshot_directory;
}

enum mh_error_code
mh_application_open_with_services(int argc, char **argv,
	const struct mh_app_services *services, struct mh_application **output,
	struct mh_error *error_out)
{
	struct mh_application *app;
	int result;
	struct mh_error clock_error;

	if (argc < 1 || argv == NULL || services == NULL ||
		services->clock == NULL || services->storage == NULL ||
		services->resources == NULL || services->user_data == NULL ||
		services->rendering == NULL ||
		services->rendering->windows == NULL ||
		services->inputs == NULL ||
		services->rendering->canvases == NULL ||
		services->rendering->renderers == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid application arguments");

	if (active_application != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"an application is already open");

	if (services->game_directory != NULL &&
		strlen(services->game_directory) >= MH_APPLICATION_PATH_SIZE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"default game directory is too long");

	if (mh_storage_status(services->storage) == MH_STORAGE_PENDING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"storage operation is already pending");

	app = (struct mh_application *)calloc(1, sizeof(*app));

	if (app == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening application");

	if (services->lifecycle != NULL) {
		if (services->lifecycle->set_suspended == NULL ||
			mh_paused_clock_init(&app->clock, services->clock,
				error_out) != MH_ERROR_NONE) {
			free(app);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid host lifecycle services");
		}

		app->lifecycle = *services->lifecycle;
	}

	initialize_services(app, services);

	if (mh_game_timer_start(&app->timer, &clock_error) != MH_ERROR_NONE) {
		fprintf(stderr, "Game clock: %s\n", clock_error.message);
		result = 1;
	} else
		result = mh_application_parse_arguments(app, argc, argv);

	initialize_session_services(app, services);

	if (result >= 0)
		mh_application_stop(app, result);

	active_application = app;
	*output = app;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_app_status
begin_opening(struct mh_application *app)
{
	struct mh_error error;

	if (mh_application_load_content(app, &error) != MH_ERROR_NONE) {
		if (!app->assets_explicit && error.code == MH_ERROR_NOT_FOUND)
			fputs("No game assets found.\n"
			"Use --assets PATH or place a game .ass package beside openmotor.\n",
				stderr);
		else
			fprintf(stderr,
				"Assets \"%s\": %s\n"
				"Use --assets PATH to select a game directory or .ass package.\n",
				app->asset_root, error.message);
		return mh_application_stop(app, 1);
	}

	if (mh_application_content_saves(app, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "Content saves: %s\n", error.message);
		return mh_application_stop(app, 1);
	}

	if (mh_application_load_settings(app, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "Settings: %s\n", error.message);
		return mh_application_stop(app, 1);
	}

	if (app->race_frontend != NULL) {
		if (mh_race_frontend_open(app, &error) != MH_ERROR_NONE) {
			fprintf(stderr, "Race frontend: %s\n", error.message);
			return mh_application_stop(app, 1);
		}

		return MH_APP_RUNNING;
	}

	if (!app->no_intro && app->capture_path == NULL &&
		app->frame_limit == 0 &&
		app->start_page == MH_MENUS_PAGE_MAIN &&
		app->startup_dialog == 0) {
		if (app->session_options.videos != NULL &&
			app->content_intro.count != 0 &&
			mh_application_open_window(app, "Motorhead movie",
				&error) != MH_ERROR_NONE) {
			fprintf(stderr, "Opening window: %s\n", error.message);
			return mh_application_stop(app, 1);
		}

		if (mh_opening_begin(app->session_options.videos,
			app->resources, app->windows, app->window,
			app->asset_root,
			app->audio_mode == MH_MENUS_AUDIO_DISABLED,
			app->fullscreen, &app->content_intro,
			&app->opening_outcome, &app->opening,
			&error) != MH_ERROR_NONE) {
			fprintf(stderr, "Opening movies: %s\n", error.message);
			return mh_application_stop(app, 1);
		}

		app->phase = APPLICATION_OPENING;
	} else
		app->phase = APPLICATION_MENUS_INIT;

	return MH_APP_RUNNING;
}

static enum mh_app_status
advance_opening(struct mh_application *app)
{
	enum mh_app_status status;

	status = mh_activity_step(&app->opening);
	app->delay = mh_activity_delay(&app->opening);

	if (status == MH_APP_RUNNING)
		return status;

	mh_activity_close(&app->opening);

	if (status == MH_APP_FAILED ||
		app->opening_outcome == MH_OPENING_FAILED)
		return mh_application_stop(app, 1);

	if (app->opening_outcome == MH_OPENING_CLOSED)
		return mh_application_stop(app, 0);

	app->phase = APPLICATION_MENUS_INIT;

	return MH_APP_RUNNING;
}

static enum mh_app_status
step_application(struct mh_application *app)
{
	if (app == NULL)
		return MH_APP_FAILED;

	app->delay = 0;

	if (app->status != MH_APP_RUNNING)
		return app->status;

	switch (app->phase) {
	case APPLICATION_START:
		return begin_opening(app);
	case APPLICATION_OPENING:
		return advance_opening(app);
	case APPLICATION_MENUS_INIT:
		return mh_application_open_menus(app);
	case APPLICATION_MENU:
		return mh_application_step_menus(app);
	case APPLICATION_RACE_FRONTEND:
		return mh_race_frontend_step(app);
	}

	return mh_application_stop(app, 1);
}

void
mh_application_begin_storage(
	struct mh_application *app, enum mh_storage_operation operation)
{
	app->storage_operation = operation;

	app->storage_resume_status = app->status;
	app->storage_resume_delay = app->delay;
	app->storage_waiting = 1;
	app->storage_started = 0;
	app->status = MH_APP_RUNNING;
}

static enum mh_app_status
advance_storage(struct mh_application *app)
{
	enum mh_error_code result;
	enum mh_storage_status status;

	result = MH_ERROR_NONE;

	if (!app->storage_started) {
		result = mh_storage_begin(
			app->storage, app->storage_operation, NULL);
		app->storage_started = 1;
		status = mh_storage_status(app->storage);
	} else
		status = mh_storage_step(app->storage);

	if (result != MH_ERROR_NONE || status == MH_STORAGE_FAILED) {
		/*
		 * The storage service retains its failure while the host
		 * decides.
		 */
		app->storage_failed = 1;
		app->status = MH_APP_FAILED;
		return app->status;
	}

	if (status == MH_STORAGE_PENDING) {
		app->delay = 1;
		return MH_APP_RUNNING;
	}

	if (app->storage_operation == MH_STORAGE_PREPARE)
		app->storage_prepared = 1;

	app->storage_waiting = 0;
	app->status = app->storage_resume_status;
	app->delay = app->storage_resume_delay;

	return app->status;
}

enum mh_app_status
mh_application_step(struct mh_application *app)
{
	if (app == NULL)
		return MH_APP_FAILED;

	app->delay = 0;

	if (app->status != MH_APP_RUNNING)
		return app->status;

	if (!app->storage_prepared) {
		if (!app->storage_waiting)
			mh_application_begin_storage(app, MH_STORAGE_PREPARE);

		advance_storage(app);

		if (!app->storage_prepared)
			return app->status;
	} else if (app->storage_waiting)
		return advance_storage(app);

	if (app->suspended)
		return MH_APP_RUNNING;

	step_application(app);

	if (app->menus != NULL || app->race_frontend != NULL) {
		if (app->status != MH_APP_RUNNING && !app->finalized) {
			if (app->menus != NULL)
				finish_menus(app);

			app->finalized = 1;
		}

		/* Backends can treat a clean view as an immediate no-op. */
		mh_application_begin_storage(app, MH_STORAGE_COMMIT);

		return advance_storage(app);
	}

	return app->status;
}

enum mh_error_code
mh_application_storage_error(
	const struct mh_application *app, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (app == NULL || !app->storage_failed)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_storage_error(app->storage, error_out);

	return status == MH_ERROR_NONE
		? mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot begin storage operation")
		: status;
}

enum mh_error_code
mh_application_retry_storage(
	struct mh_application *app, struct mh_error *error_out)
{
	if (app == NULL || !app->storage_failed)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"application has no failed storage operation");

	app->storage_failed = 0;
	app->storage_started = 0;
	app->status = MH_APP_RUNNING;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned long
mh_application_delay(const struct mh_application *app)
{
	return app == NULL ? 0 : app->delay;
}

int
mh_application_close(struct mh_application *app)
{
	struct mh_error diagnostic;

	int result;

	if (app == NULL)
		return 1;

	if (app->storage_failed) {
		mh_application_storage_error(app, &diagnostic);
		fprintf(stderr, "Storage: %s\n", diagnostic.message);
	}

	mh_storage_close(app->storage);
	mh_activity_close(&app->opening);
	mh_race_frontend_close(app);
	result = app->storage_failed ? 1 : app->exit_code;

	if (app->menus != NULL) {
		if (app->status == MH_APP_RUNNING || app->storage_failed)
			app->success = 0;

		result = close_menus(app);
	}

	mh_application_close_window(app);
	mh_settings_store_close(app->settings_store);
	active_application = NULL;
	mh_user_root_close(app->content_user_data);
	mh_vfs_close(app->asset_mount);
	free(app);

	return result;
}
