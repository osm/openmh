#include <stdlib.h>
#include <string.h>

#include "client/application/multiplayer_internal.h"
#include "client/application/preferences.h"
#include "client/application/race_frontend_internal.h"
#include "client/settings/render_choice.h"
#include "engine/input/keyboard.h"
#include "engine/storage/demo_file.h"

int
mh_race_frontend_idle(const struct mh_application *app)
{
	return app != NULL && app->race_frontend != NULL &&
		app->phase == APPLICATION_RACE_FRONTEND &&
		!app->storage_waiting && !app->storage_failed &&
		app->status == MH_APP_RUNNING &&
		!mh_application_network_active(app) &&
		app->race_frontend->state != MH_FRONTEND_RACING &&
		app->race_frontend->state != MH_FRONTEND_STARTING;
}

enum mh_error_code
mh_application_use_race_frontend(
	struct mh_application *app, struct mh_error *error_out)
{
	if (app == NULL || app->phase != APPLICATION_START ||
		app->race_frontend != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"application has already started");

	app->race_frontend = calloc(1, sizeof(*app->race_frontend));

	if (app->race_frontend == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing race frontend");

	mh_activity_init(&app->race_frontend->activity);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
loading_stage(void *context, enum mh_race_load_stage stage,
	struct mh_error *error_out)
{
	struct mh_race_frontend *f;

	f = context;
	f->progress = ((unsigned int)stage + 1) * 100 / (MH_RACE_LOAD_FONT + 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_frontend_open(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	enum mh_error_code status;

	if (app->preferences == NULL)
		app->preferences = &app->defaults;

	if (app->renderer < MH_RENDER_CHOICE_COUNT)
		app->preferences->renderer = app->renderer;

	f = app->race_frontend;
	f->random = app->settings_seed;
	status = mh_race_frontend_catalogs(app, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	f->observer.context = f;
	f->observer.complete = loading_stage;
	f->state = MH_FRONTEND_READY;
	app->phase = APPLICATION_RACE_FRONTEND;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_frontend_state
mh_application_race_state(const struct mh_application *app)
{
	return app == NULL || app->race_frontend == NULL || app->storage_waiting
		? MH_FRONTEND_PREPARING
		: app->race_frontend->state;
}

enum mh_error_code
mh_application_race_defaults(const struct mh_application *app,
	struct mh_race_request *output, struct mh_error *error_out)
{
	if (app == NULL || output == NULL ||
		app->phase != APPLICATION_RACE_FRONTEND)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"race frontend is not ready");

	mh_race_frontend_defaults(app, output);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_application_race_completed(const struct mh_application *app)
{
	return app != NULL && app->race_frontend != NULL &&
		app->race_frontend->control.finished;
}

enum mh_error_code
mh_application_start_race(struct mh_application *app,
	const struct mh_race_request *request, struct mh_error *error_out)
{
	struct mh_settings settings;
	enum mh_error_code status;

	if (!mh_race_frontend_idle(app))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "race frontend is busy");

	status = mh_race_frontend_configure(app, request, &settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	app->race_frontend->request = *request;
	app->race_frontend->state = MH_FRONTEND_STARTING;
	app->race_frontend->error[0] = 0;
	app->race_frontend->progress = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_bind_key(struct mh_application *app, unsigned int profile,
	unsigned int action, const char *key, struct mh_error *error_out)
{
	struct mh_control_profile *target;
	unsigned int code;

	if (!mh_race_frontend_idle(app))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "race frontend is busy");

	if (profile >= app->race_frontend->controls.count || key == NULL ||
		!mh_application_control_action_available(app, action))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid keyboard binding");

	/*
	 * Escape belongs to the page; undefined and device codes are not keys.
	 */
	for (code = 1; code < MH_KEY_MOUSE_IN; code++) {
		if (code != MH_KEY_ESCAPE &&
			strncmp(mh_control_key_name(code), "Undefined", 9) !=
				0 &&
			strcmp(mh_control_key_name(code), key) == 0)
			break;
	}

	if (code == MH_KEY_MOUSE_IN)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unknown or reserved keyboard key");

	target = &app->race_frontend->controls.profiles[profile];

	if (target->layout.bindings[action] != code) {
		target->layout.bindings[action] = code;
		target->dirty = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
save_session_settings(struct mh_application *app,
	const struct mh_settings *settings, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	enum mh_error_code status;

	f = app->race_frontend;
	status = mh_control_store_save(
		f->control_store, &f->controls, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*app->preferences = *settings;
	f->runtime_settings = *settings;

	if (f->request.music_track != 0)
		f->music[settings->track] = f->request.music_track;

	/*
	 * No recording-name dialog here; retain the saved preference for menus.
	 */
	f->runtime_settings.record_race = 0;
	f->controls.selected = f->request.controls;

	if (app->settings_store != NULL) {
		status = mh_settings_store_save(app->settings_store, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
prepare_session_options(struct mh_application *app,
	const struct mh_settings *settings, struct mh_session_options *options)
{
	struct mh_race_frontend *f;

	f = app->race_frontend;
	*options = app->session_options;
	options->window = app->window;
	options->controls = &f->bindings;
	options->course_music = f->music;
	options->course_music_count = f->tracks.count;
	options->record_class = f->record_class;
	options->wait_for_loading = 0;
	options->screenshot_encoder =
		app->screenshot_encoders[settings->av.screenshot_format];
	options->single_control = &f->control;
	options->load_observer = &f->observer;
	memset(&f->control, 0, sizeof(f->control));
}

enum mh_error_code
mh_race_frontend_prepare_session(struct mh_application *app, int load_profiles,
	struct mh_session_options *options, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	struct mh_settings settings;
	enum mh_error_code status;

	f = app->race_frontend;
	status = mh_race_frontend_configure(
		app, &f->request, &settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_control_layout_apply(
		&f->controls.profiles[f->request.controls].layout, &f->bindings,
		error_out);

	if (status == MH_ERROR_NONE && load_profiles && settings.game_type == 0)
		status = mh_profile_catalog_load_from(
			app->session_options.profile_source, app->resources,
			app->asset_root, &f->horns, &f->random, &f->profiles,
			error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_application_open_window(app, "Motorhead", error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = save_session_settings(app, &settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	prepare_session_options(app, &settings, options);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
start_session(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_race_frontend *f;
	struct mh_session_options options;
	enum mh_error_code status;

	f = app->race_frontend;
	status = mh_race_frontend_prepare_session(app, 1, &options, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_single_session_open(app->asset_root, &f->profiles, &f->horns,
		&f->runtime_settings, &options, &f->random, &f->quit, &f->take,
		&f->activity, error_out);
}

void
mh_race_frontend_failed(
	struct mh_race_frontend *f, const struct mh_error *error)
{
	size_t length;

	length = strlen(error->message);

	if (length >= sizeof(f->error))
		length = sizeof(f->error) - 1;

	memcpy(f->error, error->message, length);
	f->error[length] = 0;
	f->state = MH_FRONTEND_FAILED;
}

enum mh_app_status
mh_race_frontend_step(struct mh_application *app)
{
	struct mh_race_frontend *f;
	struct mh_error error;
	enum mh_app_status state;

	f = app->race_frontend;

	if (mh_application_network_active(app))
		return mh_multiplayer_step(app);

	if (f->state == MH_FRONTEND_STARTING) {
		if (start_session(app, &error) != MH_ERROR_NONE)
			mh_race_frontend_failed(f, &error);
		else
			f->state = MH_FRONTEND_RACING;
		return MH_APP_RUNNING;
	}

	if (f->state != MH_FRONTEND_RACING) {
		app->delay = 50;
		return MH_APP_RUNNING;
	}

	state = mh_activity_step(&f->activity);
	app->delay = mh_activity_delay(&f->activity);

	if (state == MH_APP_RUNNING)
		return state;

	mh_activity_error(&f->activity, &error);

	if (state == MH_APP_FAILED)
		mh_race_frontend_failed(f, &error);
	else
		f->state = MH_FRONTEND_FINISHED;
	mh_activity_close(&f->activity);
	app->preferences->av = f->runtime_settings.av;
	mh_demo_file_abort(f->take);
	f->take = NULL;

	if (app->settings_store != NULL &&
		mh_settings_store_save(app->settings_store, &error) !=
			MH_ERROR_NONE)
		mh_race_frontend_failed(f, &error);

	return MH_APP_RUNNING;
}

enum mh_error_code
mh_application_leave_race(
	struct mh_application *app, struct mh_error *error_out)
{
	if (mh_application_network_active(app))
		return mh_application_network_return(app, error_out);

	if (app == NULL || app->race_frontend == NULL ||
		app->race_frontend->state != MH_FRONTEND_RACING)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "no active race");

	app->race_frontend->control.leave = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_standings *
mh_application_race_results(const struct mh_application *app)
{
	return app == NULL || app->race_frontend == NULL
		? NULL
		: &app->race_frontend->control.results;
}

const char *
mh_application_race_error(const struct mh_application *app)
{
	return app == NULL || app->race_frontend == NULL
		? "race frontend is unavailable"
		: app->race_frontend->error;
}

unsigned int
mh_application_race_progress(const struct mh_application *app)
{
	return app == NULL || app->race_frontend == NULL
		? 0
		: app->race_frontend->progress;
}

void
mh_race_frontend_close(struct mh_application *app)
{
	struct mh_race_frontend *f;

	f = app->race_frontend;

	if (f == NULL)
		return;

	mh_multiplayer_close(app);
	mh_activity_close(&f->activity);
	mh_demo_file_abort(f->take);
	mh_control_store_close(f->control_store);
	mh_control_catalog_free(&f->controls);
	mh_graphics_catalog_close(f->graphics);
	mh_car_catalog_close(&f->cars);
	mh_track_catalog_close(&f->tracks);
	free(f);
	app->race_frontend = NULL;
}
