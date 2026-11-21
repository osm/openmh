#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "client/application/preferences.h"
#include "client/application/race_frontend_internal.h"
#include "client/settings/render_choice.h"
#include "engine/input/device_choices.h"
#include "engine/render/viewport.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "preferences:%d: %s\n", __LINE__, #x); \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned int
index_of(const char *key)
{
	struct mh_preference field;
	unsigned int i;

	for (i = 0; mh_preference_describe(i, 1, &field); i++)
		if (strcmp(field.name, key) == 0)
			return i;

	return mh_preference_count(1);
}

static struct mh_preference_update
number(const char *key, double value)
{
	struct mh_preference_update update;

	update.index = index_of(key);
	update.number = value;
	update.text = NULL;

	return update;
}

static int
check_ranges(void)
{
	struct mh_settings settings, before;
	struct mh_preference field;
	unsigned int i, j;
	struct mh_preference other;
	char unterminated[MH_SETTINGS_PLAYER_BYTES];

	CHECK(mh_settings_init(&settings, "Tester", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; mh_preference_describe(i, 1, &field); i++) {
		for (j = 0; j < i; j++) {
			CHECK(mh_preference_describe(j, 1, &other));
			CHECK(strcmp(field.name, other.name) != 0);
		}

		if (field.type == MH_PREFERENCE_TEXT)
			continue;

		CHECK(mh_preference_set(&settings, &field, field.minimum, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_preference_number(&settings, &field) == field.minimum);
		CHECK(mh_preference_set(&settings, &field, field.maximum, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_preference_number(&settings, &field) == field.maximum);
		before = settings;
		CHECK(mh_preference_set(&settings, &field, field.maximum + 1,
			NULL, NULL) == MH_ERROR_RANGE);
		CHECK(mh_preference_set(&settings, &field, field.minimum - 1,
			NULL, NULL) == MH_ERROR_RANGE);
		CHECK(mh_preference_set(&settings, &field, strtod("NaN", NULL),
			NULL, NULL) == MH_ERROR_RANGE);
		CHECK(mh_preference_set(&settings, &field,
			strtod("Infinity", NULL), NULL,
			NULL) == MH_ERROR_RANGE);
		CHECK(memcmp(&settings, &before, sizeof(settings)) == 0);
	}

	CHECK(i == mh_preference_count(1));
	CHECK(mh_preference_describe(
		index_of("graphics.perspectiveQuality"), 1, &field));
	CHECK(mh_preference_set(&settings, &field, 17, NULL, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_preference_describe(index_of("player.name"), 1, &field));
	CHECK(mh_preference_set(&settings, &field, 0, "", NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "A\nB", NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "A;B", NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "A//B", NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "1234567890123456",
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, " Driver", NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "123456789012345",
		NULL) == MH_ERROR_NONE);

	memset(unterminated, 'x', sizeof(unterminated));
	unterminated[sizeof(unterminated) - 1] = '/';
	CHECK(mh_preference_set(&settings, &field, 0, unterminated, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_preference_set(&settings, &field, 0, "A  B", NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

static int
check_updates(struct mh_application *app)
{
	struct mh_preference_update updates[4];
	struct mh_settings before, configured;
	struct mh_race_request race;

	before = *app->preferences;
	updates[0] = number("graphics.smoke", 0);
	updates[1] = number("graphics.detail", MH_DETAIL_MAX);
	updates[2] = number("graphics.namePlates", MH_AV_NAMES_FLAT);
	updates[3] = number("player.units", MH_UNITS_IMPERIAL);
	CHECK(mh_application_update_preferences(app, updates, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(app->preferences->av.smoke == 0);
	CHECK(app->preferences->av.detail_mode == MH_DETAIL_CUSTOM);
	CHECK(app->preferences->av.name_plates == MH_AV_NAMES_FLAT);
	CHECK(app->preferences->av.system == MH_UNITS_IMPERIAL);
	mh_race_frontend_defaults(app, &race);
	CHECK(mh_race_frontend_configure(app, &race, &configured, NULL) ==
		MH_ERROR_NONE);
	CHECK(configured.av.smoke == 0 &&
		configured.av.name_plates == MH_AV_NAMES_FLAT);

	updates[0] = number("player.units", MH_UNITS_METRIC);
	updates[1] = number("graphics.viewDistance", 1000);
	before = *app->preferences;
	CHECK(mh_application_update_preferences(app, updates, 2, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(app->preferences, &before, sizeof(before)) == 0);
	updates[1] = number("graphics.vsync", 0);
	CHECK(mh_application_update_preferences(app, updates, 2, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(app->preferences, &before, sizeof(before)) == 0);
	updates[1] = number("car", 2);
	CHECK(mh_application_update_preferences(app, updates, 2, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(app->preferences, &before, sizeof(before)) == 0);
	updates[1] = number("mode", 1);
	CHECK(mh_application_update_preferences(app, updates, 2, NULL) ==
		MH_ERROR_INVALID);
	updates[1] = number("graphics.renderer", MH_RENDER_CHOICE_SOFTWARE);

	if (mh_render_choice_available(MH_RENDER_CHOICE_SOFTWARE)) {
		updates[2] = number(
			"graphics.width", MH_SOFTWARE_VIEW_MAX_WIDTH + 1);
		CHECK(mh_application_update_preferences(
			app, updates, 3, NULL) == MH_ERROR_RANGE);
	}

	updates[0] = number("sound.courseMusic.1", 3);
	CHECK(mh_application_update_preferences(app, updates, 1, NULL) ==
		MH_ERROR_INVALID);
	updates[0].number = 2;
	CHECK(mh_application_update_preferences(app, updates, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(app->preferences->course_music[0].track == 2 &&
		app->race_frontend->music[0] == 2);
	app->race_frontend->state = MH_FRONTEND_RACING;
	CHECK(mh_application_update_preferences(app, updates, 1, NULL) ==
		MH_ERROR_STATE);
	app->race_frontend->state = MH_FRONTEND_READY;
	app->storage_waiting = 1;
	CHECK(mh_application_update_preferences(app, updates, 1, NULL) ==
		MH_ERROR_STATE);
	app->storage_waiting = 0;

	return 0;
}

static int
check_controls(struct mh_application *app)
{
	struct mh_control_layout layout, before, decoded;
	char text[MH_CONTROL_TEXT_CAPACITY];
	size_t size;

	before = *mh_application_control_profile(app, 0);
	layout = before;
	layout.bindings[MH_CONTROL_GAME_MENU] = MH_KEY_A;
	CHECK(mh_application_update_control_profile(app, 0, &layout, NULL) ==
		MH_ERROR_UNSUPPORTED);
	layout = before;
	layout.bindings[MH_CONTROL_ACCELERATE] = MH_KEY_ESCAPE;
	CHECK(mh_application_update_control_profile(app, 0, &layout, NULL) ==
		MH_ERROR_UNSUPPORTED);
	layout = before;
	layout.force_feedback = !before.force_feedback;
	CHECK(mh_application_update_control_profile(app, 0, &layout, NULL) ==
		MH_ERROR_UNSUPPORTED);
	layout = before;
	layout.mouse_speed[0] = 11;
	CHECK(mh_application_update_control_profile(app, 0, &layout, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(mh_application_control_profile(app, 0), &before,
		sizeof(before)) == 0);
	layout = before;
	layout.bindings[MH_CONTROL_ACCELERATE] = MH_KEY_W;

	if (mh_input_scan_available(MH_KEY_MOUSE_LEFT)) {
		layout.mouse_speed[0] = 1.5f;
		layout.bindings[MH_CONTROL_REAR_VIEW] =
			MH_KEY_MOUSE_BUTTON_MIDDLE;
	}

	CHECK(mh_application_update_control_profile(app, 0, &layout, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_control_layout_encode(&layout, NULL, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_control_layout_read((const unsigned char *)text, size, NULL,
		&decoded, NULL) == MH_ERROR_NONE);
	CHECK(decoded.bindings[MH_CONTROL_ACCELERATE] == MH_KEY_W);
	CHECK(decoded.mouse_speed[0] == layout.mouse_speed[0]);
	CHECK(decoded.bindings[MH_CONTROL_REAR_VIEW] ==
		layout.bindings[MH_CONTROL_REAR_VIEW]);

	return 0;
}

static int
check_saved_values(struct mh_application *app)
{
	const char *car, *track, *horn;
	struct mh_settings_catalog catalog;
	struct mh_settings restored;
	char text[MH_SETTINGS_TEXT_CAPACITY];
	size_t size;
	unsigned long random;

	car = "Fixture car";
	track = "Fixture track";
	horn = "Horn";
	catalog.cars.names = &car;
	catalog.cars.count = 1;
	catalog.tracks.names = &track;
	catalog.tracks.count = 1;
	catalog.horns.names = &horn;
	catalog.horns.count = 1;
	catalog.renderers.names = mh_render_choice_names();
	catalog.renderers.count = MH_RENDER_CHOICE_COUNT;
	random = 1;
	CHECK(mh_settings_encode(app->preferences, &catalog, text, sizeof(text),
		&size, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_init(&restored, "Other", 0, 0,
		mh_render_choice_default(), NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_parse((const unsigned char *)text, size, &catalog,
		&random, &restored, NULL) == MH_ERROR_NONE);
	CHECK(restored.av.system == MH_UNITS_IMPERIAL);
	CHECK(restored.av.detail_mode == MH_DETAIL_CUSTOM);
	CHECK(restored.av.smoke == 0 &&
		restored.av.name_plates == MH_AV_NAMES_FLAT);

	return 0;
}

int
main(void)
{
	static struct mh_application app;
	static struct mh_race_frontend frontend;
	struct mh_car car;
	struct mh_track track;
	const struct mh_resource_source *resources;

	CHECK(check_ranges() == 0);
	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	strcpy(car.name, "Fixture car");
	strcpy(track.name, "Fixture track");
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_control_store_open_from(resources, "Controls", NULL,
		"Keyboard", &frontend.controls, &frontend.control_store,
		NULL) == MH_ERROR_NONE);
	app.preferences = &app.defaults;
	app.race_frontend = &frontend;
	app.phase = APPLICATION_RACE_FRONTEND;
	app.status = MH_APP_RUNNING;
	frontend.state = MH_FRONTEND_READY;
	frontend.cars.entries = &car;
	frontend.cars.count = 1;
	frontend.tracks.entries = &track;
	frontend.tracks.count = 1;
	frontend.car_available[0] = frontend.track_available[0] = 1;
	frontend.horns.count = 1;
	strcpy(frontend.horns.horns[0].name, "Horn");
	frontend.music_catalog.last_track = 2;
	frontend.music_catalog.seconds[2] = 120;
	CHECK(mh_settings_init(app.preferences, "Driver", 100, 100,
		mh_render_choice_default(), NULL) == MH_ERROR_NONE);
	app.preferences->av.race_audio = mh_race_audio_default();
	strcpy(app.preferences->control_name,
		frontend.controls.profiles[0].layout.name);
	app.preferences->course_music_count = 1;
	app.preferences->course_music[0].division = 1;
	CHECK(check_updates(&app) == 0);
	CHECK(check_controls(&app) == 0);
	CHECK(check_saved_values(&app) == 0);
	mh_control_store_close(frontend.control_store);
	mh_control_catalog_free(&frontend.controls);
	mh_memory_resources_close(resources);
	puts("public preferences passed");

	return 0;
}
