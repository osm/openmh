#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/preferences.h"
#include "client/hosts/browser_preferences_api.h"
#include "engine/core/text.h"
#include "engine/input/device_choices.h"
#include "engine/input/keyboard.h"

#define PREFERENCE_TEXT_BYTES MH_SETTINGS_HISCORE_BYTES
/*
 * Control ABI fields: feedback, three mouse axes, then named action bindings.
 */
#define CONTROL_SPEED_FIRST 1
#define CONTROL_BINDING_FIRST (CONTROL_SPEED_FIRST + MH_CONTROL_MOUSE_AXES)
#define CONTROL_FIELD_COUNT (CONTROL_BINDING_FIRST + MH_CONTROL_FILE_BINDINGS)
#define CONTROL_ACTION_FIRST CONTROL_FIELD_COUNT
#define CONTROL_FEEDBACK_AVAILABLE                                             \
	(CONTROL_ACTION_FIRST + MH_CONTROL_FILE_BINDINGS)

#define CONTROL_MOUSE_AVAILABLE (CONTROL_FEEDBACK_AVAILABLE + 1)
#define CONTROL_MOUSE_MINIMUM (CONTROL_MOUSE_AVAILABLE + 1)
#define CONTROL_MOUSE_MAXIMUM (CONTROL_MOUSE_MINIMUM + 1)

static struct mh_application *application;
static char failure[512];
static struct mh_preference_update *pending;
static char (*pending_text)[PREFERENCE_TEXT_BYTES];
static unsigned int pending_count, pending_capacity;
static struct mh_control_layout control;
static unsigned int control_profile;
static int control_pending;

static int
result(enum mh_error_code status, const struct mh_error *error)
{
	failure[0] = 0;

	if (status != MH_ERROR_NONE)
		mh_text_copy(failure, sizeof(failure), error->message, NULL);

	return status == MH_ERROR_NONE;
}

static int
invalid(const char *message)
{
	mh_text_copy(failure, sizeof(failure), message, NULL);

	return 0;
}

static void
clear_pending(void)
{
	free(pending);
	free(pending_text);
	pending = NULL;
	pending_text = NULL;
	pending_count = pending_capacity = 0;
}

void
mh_browser_preferences_attach(struct mh_application *app)
{
	clear_pending();
	control_pending = 0;
	application = app;
}

const char *
mh_browser_preferences_error(void)
{
	return failure;
}

unsigned int
mh_browser_preferences_count(void)
{
	const struct mh_settings *settings;

	settings = mh_application_preferences(application);

	return settings == NULL
		? 0
		: mh_preference_count(settings->course_music_count);
}

const char *
mh_browser_preferences_text(unsigned int index, unsigned int field)
{
	static struct mh_preference description;
	const struct mh_settings *settings;

	settings = mh_application_preferences(application);

	if (settings == NULL ||
		!mh_preference_describe(
			index, settings->course_music_count, &description))
		return NULL;

	switch (field) {
	case 0:
		return description.name;
	case 1:
		return description.choices;
	case 2:
		return mh_application_preference_reason(
			application, settings, &description);
	case 3:
		return mh_preference_text(settings, &description);
	}

	return NULL;
}

double
mh_browser_preferences_number(unsigned int index, unsigned int field)
{
	struct mh_preference description;
	const struct mh_settings *settings;

	settings = mh_application_preferences(application);

	if (settings == NULL ||
		!mh_preference_describe(
			index, settings->course_music_count, &description))
		return -1;

	switch (field) {
	case 0:
		return description.type;
	case 1:
		return description.minimum;
	case 2:
		return description.maximum;
	case 3:
		return description.step;
	case 4:
		return mh_preference_number(settings, &description);
	}

	return -1;
}

int
mh_browser_preferences_begin(unsigned int count)
{
	clear_pending();

	if (count == 0 || count > mh_browser_preferences_count())
		return invalid("Invalid preference update count.");

	pending = calloc(count, sizeof(*pending));
	pending_text = calloc(count, sizeof(*pending_text));

	if (pending == NULL || pending_text == NULL) {
		clear_pending();
		return invalid("Out of memory staging preferences.");
	}

	pending_capacity = count;

	return 1;
}

int
mh_browser_preferences_set(unsigned int index, double number, const char *text)
{
	if (pending_count >= pending_capacity ||
		index >= mh_browser_preferences_count()) {
		clear_pending();
		return invalid("Invalid preference update.");
	}

	if (text != NULL &&
		mh_text_copy(pending_text[pending_count], sizeof(*pending_text),
			text, NULL) != MH_ERROR_NONE) {
		clear_pending();
		return invalid("Preference text is too long.");
	}

	pending[pending_count].index = index;
	pending[pending_count].number = number;
	pending[pending_count].text = pending_text[pending_count];
	pending_count++;

	return 1;
}

int
mh_browser_preferences_commit(void)
{
	struct mh_error error;
	enum mh_error_code status;

	if (pending == NULL || pending_count != pending_capacity) {
		clear_pending();
		return invalid("Incomplete preference update.");
	}

	status = mh_application_update_preferences(
		application, pending, pending_count, &error);
	clear_pending();

	return result(status, &error);
}

const char *
mh_browser_controls_key(unsigned int scan)
{
	return mh_application_control_scan_available(scan)
		? mh_control_key_name(scan)
		: NULL;
}

double
mh_browser_controls_get(unsigned int profile, unsigned int field)
{
	const struct mh_control_layout *layout;

	layout = mh_application_control_profile(application, profile);

	if (layout == NULL)
		return -1;

	if (field == 0)
		return layout->force_feedback;

	if (field < CONTROL_BINDING_FIRST)
		return layout->mouse_speed[field - CONTROL_SPEED_FIRST];

	if (field < CONTROL_FIELD_COUNT)
		return layout->bindings[field - CONTROL_BINDING_FIRST];

	if (field < CONTROL_FEEDBACK_AVAILABLE)
		return mh_application_control_action_available(
			application, field - CONTROL_ACTION_FIRST);

	if (field == CONTROL_FEEDBACK_AVAILABLE)
		return mh_application_control_feedback_available(application);

	if (field == CONTROL_MOUSE_AVAILABLE)
		return mh_input_scan_available(MH_KEY_MOUSE_LEFT);

	if (field == CONTROL_MOUSE_MINIMUM)
		return MH_PREFERENCE_MOUSE_SPEED_MIN;

	if (field == CONTROL_MOUSE_MAXIMUM)
		return MH_PREFERENCE_MOUSE_SPEED_MAX;

	return -1;
}

int
mh_browser_controls_begin(unsigned int profile)
{
	const struct mh_control_layout *layout;

	control_pending = 0;
	layout = mh_application_control_profile(application, profile);

	if (layout == NULL)
		return invalid("Unknown control profile.");

	control = *layout;
	control_profile = profile;
	control_pending = 1;

	return 1;
}

int
mh_browser_controls_set(unsigned int field, double value)
{
	if (!control_pending || field >= CONTROL_FIELD_COUNT)
		return invalid("Invalid control update.");

	if (field >= CONTROL_SPEED_FIRST && field < CONTROL_BINDING_FIRST) {
		if (!(value >= MH_PREFERENCE_MOUSE_SPEED_MIN &&
			value <= MH_PREFERENCE_MOUSE_SPEED_MAX))
			return invalid(
				"Mouse speed must be between 0.01 and 10.");

		control.mouse_speed[field - CONTROL_SPEED_FIRST] = (float)value;
	} else {
		if (!(value >= 0 && value <= MH_U32_MAX) ||
			floor(value) != value)
			return invalid("Invalid control value.");

		if (field == 0) {
			if (value > 1)
				return invalid("Invalid force feedback value.");

			control.force_feedback = (unsigned int)value;
		} else
			control.bindings[field - CONTROL_BINDING_FIRST] =
				(unsigned long)value;
	}

	return 1;
}

int
mh_browser_controls_commit(void)
{
	struct mh_error error;
	enum mh_error_code status;

	if (!control_pending)
		return invalid("No pending control update.");

	control_pending = 0;
	status = mh_application_update_control_profile(
		application, control_profile, &control, &error);

	return result(status, &error);
}
