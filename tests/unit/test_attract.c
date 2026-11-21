#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/attract.h"
#include "engine/ui/hud.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "attract line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned int
view_flags(const struct mh_attract_view *view)
{
	return view->in_car | view->requested_in_car * 2 | view->chase * 4 |
		view->track * 8 | view->intro * 16;
}

static void
set_view(struct mh_attract_view *view, unsigned int viewed, unsigned int bits,
	unsigned long phase)
{
	view->viewed = viewed;

	view->phase = phase;
	view->in_car = bits & 1;
	view->requested_in_car = (bits >> 1) & 1;
	view->chase = (bits >> 2) & 1;
	view->track = (bits >> 3) & 1;
	view->intro = (bits >> 4) & 1;
}

static int
test_field_frame(void)
{
	static struct mh_field field, saved_field;
	struct mh_drive_frame sample;
	struct mh_attract_frame frame, saved_frame;
	struct mh_camera_pose pose;
	unsigned long phase;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(&sample, 0, sizeof(sample));
	field.attract = 1;
	field.count = 8;
	field.active_mask = 0x89;
	field.viewed_entry = 7;
	field.listener_mask = 0x89;
	field.listener_revision = 9;
	field.cars[7].camera_mode = MH_CAMERA_IN_CAR;
	field.cars[7].camera_saved_in_car = 1;
	field.cars[7].camera_intro = 1;
	field.cars[7].camera_goal = 1;

	for (i = 0; i < 3; i++) {
		field.cars[7].camera.position[i] = (float)(i + 1);
		field.cars[7].eye[i] = (float)(i + 7);
	}

	pose = field.cars[7].camera;
	phase = 7;
	sample.seconds = .251;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(phase == 7 && field.cars[7].camera_mode == MH_CAMERA_IN_CAR &&
		field.listener_mask == 0x89 && field.listener_revision == 9);
	sample.time.go_ms = 12000;
	sample.seconds = 12.25;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(phase == 7 && field.cars[7].camera_mode == MH_CAMERA_IN_CAR &&
		field.cars[7].camera_saved_in_car);
	CHECK(!field.cars[7].camera_intro && field.cars[7].camera_goal &&
		field.listener_mask == 0x89);
	sample.seconds = 22.25;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(phase == 0 && field.viewed_entry == 0 &&
		field.listener_mask == 9 && field.listener_revision == 10);
	CHECK(field.cars[0].camera_mode == MH_CAMERA_TRACK &&
		!field.cars[0].camera_intro && field.cars[0].camera_goal);
	CHECK(memcmp(&field.cars[0].camera, &pose, sizeof(pose)) == 0 &&
		field.cars[0].eye[0] == 7);
	sample.seconds = 23;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.viewed_entry == 0 && field.listener_mask == 9 &&
		field.listener_revision == 10);
	field.listener_mask = 8;
	sample.seconds = 32.25;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.cars[0].camera_mode == MH_CAMERA_IN_CAR &&
		field.listener_mask == 8 && field.listener_revision == 10);

	/*
	 * A cycle back to the same sole active car still explicitly releases.
	 */
	field.active_mask = 1;
	field.listener_mask = 1;
	sample.seconds = 42.25;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.viewed_entry == 0 && field.listener_mask == 0 &&
		field.listener_revision == 11);
	field.active_mask = 0;
	saved_field = field;
	saved_frame = frame;
	phase = 6;
	CHECK(mh_attract_field_frame(&field, &sample, &phase, &frame, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved_field, &field, sizeof(field)) == 0 &&
		memcmp(&saved_frame, &frame, sizeof(frame)) == 0 && phase == 6);

	return 0;
}

static int
test_course_selection(void)
{
	struct mh_attract_course courses[3];
	char previous[16];
	unsigned long random;
	unsigned int selected;

	memset(courses, 0, sizeof(courses));
	strcpy(courses[0].name, "ATLANTIKA");
	courses[0].division = 109;
	strcpy(courses[1].name, "Invalid");
	courses[1].division = 310;
	strcpy(courses[2].name, "Alternate");
	courses[2].division = 309;
	random = 1;
	selected = 99;
	strcpy(previous, "atlantika");
	CHECK(mh_attract_course_select(courses, 3, previous, &random, &selected,
		NULL) == MH_ERROR_NONE);
	CHECK(selected == 2);
	courses[2].division = 400;
	random = 1;
	selected = 99;
	strcpy(previous, "AtLaNtIkA");
	CHECK(mh_attract_course_select(courses, 3, previous, &random, &selected,
		NULL) != MH_ERROR_NONE);
	CHECK(random == 1 && selected == 99);
	memset(courses[1].name, 'x', 16);
	previous[0] = 0;
	CHECK(mh_attract_course_select(courses, 3, previous, &random, &selected,
		NULL) != MH_ERROR_NONE);
	CHECK(random == 1 && selected == 99);
	CHECK(mh_attract_course_select(courses, 0, previous, &random, &selected,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_attract_course_select(NULL, 3, previous, &random, &selected,
		NULL) != MH_ERROR_NONE);

	return 0;
}

static int
test_runtime_settings(void)
{
	struct mh_settings saved, runtime, expected;

	CHECK(mh_settings_init(&saved, "Player", 123, 234, 0, NULL) ==
		MH_ERROR_NONE);
	saved.game_type = 1;
	saved.av.motion_blur = 1;
	saved.av.skid_marks = 0;
	saved.av.view_distance = 137;
	saved.av.width = 1280;
	saved.av.height = 720;
	saved.record_race = 1;
	expected = saved;
	CHECK(mh_attract_settings(&saved, 6, &runtime, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved, &expected, sizeof(saved)) == 0);
	CHECK(runtime.game_type == 5 && runtime.track == 6 &&
		runtime.laps == 25 && runtime.difficulty == 2);
	CHECK(runtime.av.motion_blur == 1 && runtime.av.skid_marks == 0 &&
		runtime.av.view_distance == 137);
	CHECK(runtime.av.effects_volume == 123 &&
		runtime.av.music_volume == 234 && runtime.record_race == 1);
	CHECK(runtime.av.width == 1280 && runtime.av.height == 720);
	CHECK(mh_attract_settings(&saved, 8, &runtime, NULL) == MH_ERROR_NONE &&
		runtime.track == 8);
	expected = runtime;
	CHECK(mh_attract_settings(&saved, 4096, &runtime, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&runtime, &expected, sizeof(runtime)) == 0);

	return 0;
}

static int
test_idle_clock(void)
{
	unsigned long started;
	int requested;

	CHECK(mh_attract_idle_delay(1, 0, 0) == 30000 &&
		mh_attract_idle_delay(0, 0, 0) == 1000000000UL);
	CHECK(mh_attract_idle_delay(1, 5, 0) == 10000 &&
		mh_attract_idle_delay(0, 5, 0) == 10000);
	CHECK(mh_attract_idle_delay(1, 5, 1) == 30000);
	started = 100;
	requested = -1;
	CHECK(mh_attract_idle_poll(&started, 30000, 30100, -1, &requested,
		NULL) == MH_ERROR_NONE &&
		!requested && started == 100);
	CHECK(mh_attract_idle_poll(&started, 30000, 30100.125L, 30101.75L,
		&requested, NULL) == MH_ERROR_NONE);
	CHECK(requested && started == 30101);
	requested = 77;
	CHECK(mh_attract_idle_poll(&started, 30000, 90000, -1, &requested,
		NULL) != MH_ERROR_NONE &&
		requested == 77 && started == 30101);
	CHECK(mh_attract_idle_poll(&started, 30000, -1, 0, &requested, NULL) !=
			MH_ERROR_NONE &&
		requested == 77);
	CHECK(mh_attract_idle_poll(&started, 30000, 4294967297.25L,
		4294967298.75L, &requested, NULL) == MH_ERROR_NONE);
	CHECK(requested && started == 2);

	return 0;
}

static int
test_camera_frames(void)
{
	struct mh_attract_view view;
	struct mh_attract_frame frame, saved_frame;

	set_view(&view, 7, 31, 0);
	CHECK(mh_attract_frame(&view, 8, 0x81, 0, 251, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(frame.label && !frame.finished && frame.release_mask == 0 &&
		view_flags(&frame.view) == 31);
	CHECK(mh_attract_frame(&view, 8, 0x81, 100, 10099.999L, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(view_flags(&frame.view) == 3 && frame.view.phase == 1 &&
		frame.view.viewed == 7);
	view = frame.view;
	CHECK(mh_attract_frame(&view, 8, 0x81, 100, 10100, &frame, NULL) ==
		MH_ERROR_NONE);
	CHECK(view_flags(&frame.view) == 8 && frame.view.phase == 0 &&
		frame.view.viewed == 0 && frame.release_mask == 128);
	view = frame.view;
	CHECK(mh_attract_frame(&view, 8, 0x81, 100, 10101, &frame, NULL) ==
			MH_ERROR_NONE &&
		frame.release_mask == 0 && frame.view.viewed == 0);
	CHECK(mh_attract_frame(&view, 8, 0x81, 100, 40100, &frame, NULL) ==
			MH_ERROR_NONE &&
		!frame.finished);
	CHECK(mh_attract_frame(&view, 8, 0x81, 100, 40100.125L, &frame, NULL) ==
			MH_ERROR_NONE &&
		frame.finished);
	set_view(&view, 0, 0, 7);
	CHECK(mh_attract_frame(&view, 1, 1, 100, 101, &frame, NULL) ==
			MH_ERROR_NONE &&
		frame.view.phase == 7);
	saved_frame = frame;
	CHECK(mh_attract_frame(&view, 1, 0, 100, 101, &frame, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved_frame, &frame, sizeof(frame)) == 0);
	CHECK(mh_attract_frame(&view, 1, 1, 100, -1, &frame, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved_frame, &frame, sizeof(frame)) == 0);

	return 0;
}

int
main(void)
{

	CHECK(test_field_frame() == 0);

	if (test_course_selection() || test_runtime_settings() ||
		test_idle_clock() || test_camera_frames())
		return 1;

	puts("attract: course bounds, runtime isolation, strict idle/exit clocks and retained camera cycles passed");

	return 0;
}
