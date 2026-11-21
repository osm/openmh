#include <string.h>

#include "client/application/multiplayer.h"
#include "client/application/preferences.h"
#include "client/application/race_frontend_internal.h"
#include "client/settings/render_choice.h"
#include "engine/input/device_choices.h"
#include "engine/render/viewport.h"
#include "interfaces/input/force.h"

const struct mh_settings *
mh_application_preferences(const struct mh_application *app)
{
	return app != NULL && app->phase == APPLICATION_RACE_FRONTEND
		? app->preferences
		: NULL;
}

const char *
mh_application_preference_reason(const struct mh_application *app,
	const struct mh_settings *settings, const struct mh_preference *field)
{
	const struct mh_menu_graphics_catalog *graphics;

	if (app == NULL || app->race_frontend == NULL || settings == NULL)
		return "Preferences are not ready.";

	graphics = mh_graphics_catalog_choices(app->race_frontend->graphics);

	switch (field->feature) {
	case MH_PREFERENCE_GRAPHICS:
		if (graphics == NULL || settings->renderer >= graphics->count ||
			!mh_menu_graphics_row_available(
				&graphics->devices[settings->renderer],
				field->graphics_row))
			return "Unavailable with the selected renderer.";

		break;

	case MH_PREFERENCE_PRESENTATION:
		if (graphics == NULL || !graphics->presentation)
			return "Presentation timing is controlled by the host.";

		break;

	case MH_PREFERENCE_WINDOW:
		/*
		 * External frontends own their window and user-gesture policy.
		 */
		return "Fullscreen and pointer capture are controlled by the embedding host.";

	case MH_PREFERENCE_SCREENSHOT:
		if (graphics == NULL || !graphics->screenshots)
			return "Screenshots are unavailable on this host.";

		break;

	case MH_PREFERENCE_MENUS:
		return "Used by the original menus, not the external frontend.";
	case MH_PREFERENCE_RECORDING:
		return "Recording workflows require the original menus.";
	case MH_PREFERENCE_LEGACY_NETWORK:
		return "Legacy hosting and transport preferences are not used by this frontend.";
	case MH_PREFERENCE_NETWORK:
		if (!mh_application_network_available(app))
			return "Networking is unavailable on this host.";

		break;

	case MH_PREFERENCE_MUSIC:
		if (app->race_frontend->music_catalog.last_track < 2)
			return "This game has no music.";

		break;

	default:
		break;
	}

	return "";
}

static int
choice_available(const struct mh_application *app,
	enum mh_frontend_catalog kind, unsigned int index)
{
	int available;

	return mh_application_race_choice(app, kind, index, &available) !=
		NULL &&
		available;
}

static enum mh_error_code
validate_choices(const struct mh_application *app,
	const struct mh_settings *settings, unsigned int *control,
	struct mh_error *error_out)
{
	const struct mh_race_frontend *f;
	unsigned int i;

	f = app->race_frontend;

	if (!choice_available(app, MH_FRONTEND_CARS, settings->car) ||
		!choice_available(app, MH_FRONTEND_TRACKS, settings->track) ||
		!choice_available(app, MH_FRONTEND_HORNS, settings->horn) ||
		!choice_available(
			app, MH_FRONTEND_MODES, settings->game_type) ||
		!choice_available(
			app, MH_FRONTEND_RENDERERS, settings->renderer) ||
		!choice_available(app, MH_FRONTEND_AUDIO_PROFILES,
			settings->av.race_audio))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unavailable preference choice");

	if (settings->renderer == MH_RENDER_CHOICE_SOFTWARE &&
		settings->av.width > MH_SOFTWARE_VIEW_MAX_WIDTH)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software rendering supports widths up to 1023");

	for (i = 0; i < f->controls.count; i++)
		if (strcmp(f->controls.profiles[i].layout.name,
			settings->control_name) == 0)
			break;

	if (i == f->controls.count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown control profile");

	*control = i;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_changed(const struct mh_application *app,
	const struct mh_settings *settings,
	const struct mh_preference_update *update, struct mh_error *error_out)
{
	struct mh_preference field;
	const char *reason;
	const struct mh_menu_graphics_catalog *graphics;

	mh_preference_describe(
		update->index, settings->course_music_count, &field);
	reason = mh_application_preference_reason(app, settings, &field);

	if (*reason != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED, reason);

	if (field.feature == MH_PREFERENCE_MUSIC &&
		!choice_available(app, MH_FRONTEND_MUSIC,
			(unsigned int)update->number - 2))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"music track is unavailable");

	if (strcmp(field.name, "graphics.screenshotFormat") == 0 &&
		!mh_screenshot_format_available(settings->av.screenshot_format))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"screenshot format is unavailable");

	graphics = mh_graphics_catalog_choices(app->race_frontend->graphics);

	if (strcmp(field.name, "graphics.carShading") == 0 &&
		settings->av.car_shading == MH_AV_SHADING_GLENZ &&
		(graphics == NULL || settings->renderer >= graphics->count ||
			!graphics->devices[settings->renderer].glenz))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Glenz shading is unavailable with this renderer");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_updates(struct mh_settings *settings,
	const struct mh_preference_update *updates, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_preference field;
	unsigned int i, pass;
	enum mh_error_code status;

	/*
	 * Preset first, individual overrides second, independent of object
	 * order.
	 */
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < count; i++) {
			if (!mh_preference_describe(updates[i].index,
				settings->course_music_count, &field))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"unknown preference");

			if ((strcmp(field.name, "graphics.detail") != 0) !=
				(pass != 0))
				continue;

			status = mh_preference_set(settings, &field,
				updates[i].number, updates[i].text, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
publish_preferences(struct mh_application *app,
	const struct mh_settings *settings, unsigned int control,
	struct mh_error *error_out)
{
	struct mh_settings previous;
	unsigned int i;
	enum mh_error_code status;

	previous = *app->preferences;
	*app->preferences = *settings;

	if (app->settings_store != NULL) {
		status = mh_settings_store_save(app->settings_store, error_out);

		if (status != MH_ERROR_NONE) {
			*app->preferences = previous;
			return status;
		}
	}

	app->race_frontend->controls.selected = control;

	for (i = 0;
		i < settings->course_music_count && i < MH_TRACK_CATALOG_LIMIT;
		i++)
		app->race_frontend->music[i] =
			(unsigned int)settings->course_music[i].track;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_update_preferences(struct mh_application *app,
	const struct mh_preference_update *updates, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_settings next;
	unsigned int i, control;
	enum mh_error_code status;

	if (!mh_race_frontend_idle(app))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"preferences require an idle, disconnected frontend");

	if (count != 0 && updates == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing preference updates");

	control = 0;
	next = *app->preferences;
	status = apply_updates(&next, updates, count, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_choices(app, &next, &control, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++) {
		status = validate_changed(app, &next, &updates[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return publish_preferences(app, &next, control, error_out);
}

const struct mh_control_layout *
mh_application_control_profile(
	const struct mh_application *app, unsigned int profile)
{
	return mh_application_preferences(app) != NULL &&
			profile < app->race_frontend->controls.count
		? &app->race_frontend->controls.profiles[profile].layout
		: NULL;
}

int
mh_application_control_feedback_available(const struct mh_application *app)
{
	return app != NULL && mh_input_has_force(app->inputs) &&
		mh_menus_feature_enabled(
			&app->menu_configuration, MH_MENU_FEATURE_FEEDBACK);
}

int
mh_application_control_action_available(
	const struct mh_application *app, unsigned int action)
{
	return app != NULL && action < MH_CONTROL_FILE_BINDINGS &&
		action != MH_CONTROL_GAME_MENU && action != MH_CONTROL_CHAT &&
		action != MH_CONTROL_MIRROR &&
		(action != MH_CONTROL_SCREENSHOT ||
			app->session_options.screenshot_data != NULL);
}

int
mh_application_control_scan_available(unsigned long scan)
{
	return scan < MH_KEY_COUNT && scan != MH_KEY_ESCAPE &&
		strncmp(mh_control_key_name((unsigned int)scan), "Undefined",
			9) != 0 &&
		mh_input_scan_available(scan);
}

enum mh_error_code
mh_application_update_control_profile(struct mh_application *app,
	unsigned int profile, const struct mh_control_layout *layout,
	struct mh_error *error_out)
{
	struct mh_control_profile *target, previous;
	struct mh_control_binding_set bindings;
	unsigned int i;
	enum mh_error_code status;

	if (!mh_race_frontend_idle(app))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"control updates require an idle, disconnected frontend");

	if (mh_application_control_profile(app, profile) == NULL ||
		layout == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown control profile");

	target = &app->race_frontend->controls.profiles[profile];
	status = mh_control_layout_apply(layout, &bindings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (strcmp(layout->name, target->layout.name) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"control profile names cannot be changed");

	if (layout->force_feedback != target->layout.force_feedback &&
		!mh_application_control_feedback_available(app))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"force feedback is unavailable");

	for (i = 0; i < MH_CONTROL_BINDINGS; i++) {
		if (layout->bindings[i] == target->layout.bindings[i])
			continue;

		if (!mh_application_control_action_available(app, i) ||
			!mh_application_control_scan_available(
				layout->bindings[i]))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported control binding");
	}

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++) {
		if (layout->mouse_speed[i] == target->layout.mouse_speed[i])
			continue;

		if (!(layout->mouse_speed[i] >=
				(float)MH_PREFERENCE_MOUSE_SPEED_MIN &&
			layout->mouse_speed[i] <=
				MH_PREFERENCE_MOUSE_SPEED_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"mouse speed must be between 0.01 and 10");

		if (!mh_input_scan_available(MH_KEY_MOUSE_LEFT))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"mouse input is unavailable");
	}

	previous = *target;
	target->layout = *layout;
	target->dirty = 1;
	status = mh_control_store_save(app->race_frontend->control_store,
		&app->race_frontend->controls, error_out);

	if (status != MH_ERROR_NONE)
		*target = previous;

	return status;
}
