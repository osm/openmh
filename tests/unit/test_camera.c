#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/camera.h"
#include "engine/simulation/field.h"
#include "engine/simulation/track_camera.h"

static int failures;
static struct mh_field view_field, saved_field;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/*
 * Connected presentation: retain each car's history through F4 and active
 * membership changes.
 */
static int
check_roll_frames(void)
{
	static const float intervals[6] = {0, .00001f, .125f, .25f, .5f, 1};
	struct mh_track track;
	struct mh_drive *drive;

	unsigned int n, i, j, viewed, mask;
	float seconds;
	struct mh_error error;
	enum mh_error_code status;

	memset(&track, 0, sizeof(track));
	track.view_distance = 180;
	memset(&view_field, 0, sizeof(view_field));
	view_field.count = 8;
	view_field.active_mask = 255;

	for (i = 0; i < 8; i++) {
		drive = &view_field.cars[i];
		drive->remote = 1;
		drive->track = &track;
		drive->vehicle.motion.rotation[0] =
			drive->vehicle.motion.rotation[4] =
				drive->vehicle.motion.rotation[8] = 1;
		drive->camera.rotation[0] = drive->camera.rotation[4] =
			drive->camera.rotation[8] = 1;
		drive->camera.position[1] = 2;
		drive->camera.position[2] = -5;
		drive->camera_roll.external = i & 1;
		drive->camera_roll.supported_seconds = i * .125f;
	}

	for (n = 0; n < 512; n++) {
		viewed = (n / 5) % 8;
		seconds = intervals[n % 6];
		view_field.active_mask = (n * 37 % 256) | (1U << viewed) |
			(1U << view_field.viewed_entry);
		CHECK(mh_field_select_view(&view_field, viewed, NULL) ==
			MH_ERROR_NONE);
		view_field.unavailable_mask = n * 19 % 256;
		drive = &view_field.cars[viewed];
		drive->camera_mode =
			n / 7 % 2 ? MH_CAMERA_IN_CAR : MH_CAMERA_CHASE;
		drive->look_back = n % 3 == 0;

		for (i = 0; i < 8; i++) {
			drive = &view_field.cars[i];
			drive->vehicle.motion.rotation[0] =
				drive->vehicle.motion.rotation[4] =
					(n / 11 + i) % 3 ? 1 : -1;
			drive->vehicle.motion.position[0] = i * 7;
			drive->vehicle.motion.position[2] = n * .25;
			mask = (n / 3 + i) % 16;

			for (j = 0; j < 4; j++)
				drive->vehicle.grip.wheels[j].supported =
					(mask >> j) & 1;
		}

		status = mh_field_frame_update(&view_field, seconds, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "roll frame %u: %s\n", n,
				error.message);
			return 1;
		}

		drive = &view_field.cars[viewed];

		/* A camera-only refresh must not advance the timer again. */
		saved_field = view_field;
		CHECK(mh_field_camera_update(&view_field, seconds, NULL) ==
			MH_ERROR_NONE);

		for (i = 0; i < 8; i++)
			CHECK(memcmp(&view_field.cars[i].camera_roll,
				&saved_field.cars[i].camera_roll,
				sizeof(drive->camera_roll)) == 0);

		view_field = saved_field;
	}

	return failures != 0;
}

static void
check_listener_commands(void)
{
	memset(&view_field, 0, sizeof(view_field));
	view_field.count = 3;
	view_field.active_mask = 7;
	view_field.listener_mask = 1;
	view_field.cars[0].camera_mode = MH_CAMERA_IN_CAR;
	CHECK(mh_field_select_view(&view_field, 2, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 4);
	CHECK(mh_field_restart_listener(&view_field, NULL) == MH_ERROR_NONE);
	CHECK(view_field.viewed_entry == 0 && view_field.listener_mask == 5);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_TRACK, NULL) ==
			MH_ERROR_NONE &&
		view_field.listener_mask == 4);

	/*
	 * Track view does not select the next car, or clear its retained flag.
	 */
	CHECK(mh_field_select_view(&view_field, 2, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 4);
	CHECK(view_field.cars[2].camera_mode == MH_CAMERA_TRACK &&
		!mh_field_listener_positional(&view_field));
	CHECK(mh_field_select_view(&view_field, 1, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 0);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_IN_CAR, NULL) ==
			MH_ERROR_NONE &&
		view_field.listener_mask == 2);
	CHECK(mh_field_restart_listener(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 3);
	CHECK(view_field.cars[0].camera_mode == MH_CAMERA_IN_CAR);
	CHECK(mh_field_select_view(&view_field, 2, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 6);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_CHASE, NULL) ==
			MH_ERROR_NONE &&
		view_field.listener_mask == 6);
	CHECK(mh_field_restart_listener(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.listener_mask == 7);
	CHECK(view_field.listener_revision == 10);
	CHECK(!mh_field_sound_detailed(&view_field, 3) &&
		!mh_field_sound_detailed(NULL, 0));
	CHECK(mh_field_listener_positional(NULL));
	CHECK(mh_field_restart_listener(NULL, NULL) != MH_ERROR_NONE);
	view_field.count = 0;
	saved_field = view_field;
	CHECK(mh_field_restart_listener(&view_field, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view_field, &saved_field, sizeof(view_field)) == 0);
	view_field.count = MH_RACE_MAX_CARS + 1;
	saved_field = view_field;
	CHECK(mh_field_restart_listener(&view_field, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view_field, &saved_field, sizeof(view_field)) == 0);
}

static void
check_view_distance(void)
{
	struct mh_drive drive, saved;
	struct mh_track track;

	memset(&drive, 0, sizeof(drive));
	memset(&track, 0, sizeof(track));
	drive.track = &track;
	track.view_distance = 180;
	CHECK(mh_drive_camera_far(&drive) == 180);
	CHECK(mh_drive_view_distance(&drive, 30, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_far(&drive) == 60);
	CHECK(mh_drive_view_distance(&drive, 150, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_far(&drive) == 270);
	track.view_distance = 400;
	CHECK(mh_drive_view_distance(&drive, 50, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_far(&drive) == 200);
	CHECK(mh_drive_view_distance(&drive, 150, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_far(&drive) == 300);
	drive.camera_mode = MH_CAMERA_TRACK;
	CHECK(mh_drive_view_distance(&drive, 50, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_far(&drive) == 200);
	saved = drive;
	CHECK(mh_drive_view_distance(&drive, 29, NULL) != MH_ERROR_NONE &&
		mh_drive_view_distance(&drive, 151, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&drive, &saved, sizeof(drive)) == 0);
	CHECK(mh_drive_view_distance(NULL, 100, NULL) != MH_ERROR_NONE);
}

static void
check_motion_follow(void)
{
	static const double rates[4] = {60, 144, 240, 0};
	struct mh_camera_pose car, prior, next, saved;
	float previous_car[3];
	double time, seconds, pitch, minimum, maximum;
	unsigned int rate, frame;

	for (rate = 0; rate < 4; rate++) {
		memset(&car, 0, sizeof(car));
		car.rotation[0] = car.rotation[4] = car.rotation[8] = 1;
		prior = car;
		prior.position[1] = 2;
		prior.position[2] = -4.5f;
		time = 0;
		minimum = 100;
		maximum = -100;

		for (frame = 0; frame < 1000; frame++) {
			memcpy(previous_car, car.position,
				sizeof(previous_car));
			seconds = rates[rate]
				? 1 / rates[rate]
				: (frame % 2 ? 1.0 / 40 : 1.0 / 120);
			time += seconds;
			car.position[2] = (float)(floor(time * 120) / 120 * 40);
			CHECK(mh_camera_follow_motion(&prior, previous_car,
				&car, (float)seconds, &next,
				NULL) == MH_ERROR_NONE);
			prior = next;
			pitch = atan2(next.rotation[7], next.rotation[8]);

			if (pitch < minimum)
				minimum = pitch;

			if (pitch > maximum)
				maximum = pitch;
		}

		CHECK(maximum - minimum < .0001);

		/*
		 * A turn still trails instead of becoming a rigid attached
		 * camera.
		 */
		memcpy(previous_car, car.position, sizeof(previous_car));
		car.rotation[0] = car.rotation[8] = 0;
		car.rotation[2] = -1;
		car.rotation[6] = 1;
		CHECK(mh_camera_follow_motion(&prior, previous_car, &car,
			1.0f / 60, &next, NULL) == MH_ERROR_NONE);
		CHECK(next.position[0] < 0 && next.position[0] > -4.5f);
		saved = next;
		CHECK(mh_camera_follow_motion(&prior, previous_car, &car, -1,
			&next, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&saved, &next, sizeof(next)) == 0);
		previous_car[0] = 1e7f;
		CHECK(mh_camera_follow_motion(&prior, previous_car, &car, .01f,
			&next, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&saved, &next, sizeof(next)) == 0);
	}
}

static int
check_preference(void)
{
	unsigned int previous, mode, finished, attract, goal, slot, flags;
	enum mh_camera_mode expected, result;
	struct mh_drive *viewed;

	for (mode = 0; mode < 3; mode++) {
		for (flags = 0; flags < 32; flags++) {
			previous = flags & 1;
			finished = (flags >> 1) & 1;
			attract = (flags >> 2) & 1;
			goal = (flags >> 3) & 1;
			slot = (flags >> 4) & 1;
			memset(&view_field, 0, sizeof(view_field));
			view_field.count = 2;
			view_field.viewed_entry = slot;
			view_field.attract = attract;
			viewed = &view_field.cars[slot];
			viewed->camera_mode = (enum mh_camera_mode)mode;
			viewed->camera_goal = goal;
			viewed->race.finished = finished;
			expected = (enum mh_camera_mode)previous;

			if (!finished && !attract && !goal &&
				mode != MH_CAMERA_TRACK)
				expected = (enum mh_camera_mode)mode;

			result = mh_field_camera_preference(
				&view_field, (enum mh_camera_mode)previous);
			CHECK(result == expected);
		}
	}

	return failures != 0;
}

int
main(void)
{
	struct mh_camera_pose car, prior, output, saved;
	struct mh_camera_lens lens, saved_lens;
	struct mh_camera_roll roll, saved_roll;
	struct mh_camera_motion motion;
	struct mh_camera_key keys[2];
	struct mh_camera_pose track_pose;
	struct mh_track goal_track;
	size_t selected;
	unsigned int mode;
	int supported[4] = {1, 1, 1, 0};
	float eye[3] = {0, 2, -4.5f}, target[3] = {0, 1.5f, 0};
	unsigned int i;

	CHECK(check_preference() == 0);

	CHECK(check_roll_frames() == 0);
	check_motion_follow();
	check_view_distance();
	check_listener_commands();
	memset(&car, 0, sizeof(car));
	car.rotation[0] = car.rotation[4] = car.rotation[8] = 1;
	car.position[0] = -4;
	car.position[1] = 2;
	car.position[2] = 7;
	CHECK(mh_camera_supercars_view(&car, &output, NULL) == MH_ERROR_NONE);
	CHECK(output.position[0] == -4 && output.position[1] == 27 &&
		output.position[2] == 19);
	CHECK(output.rotation[0] == 1 && output.rotation[5] == 1 &&
		output.rotation[7] == -1);
	saved = output;
	CHECK(mh_camera_supercars_view(&car, &car, NULL) == MH_ERROR_NONE &&
		memcmp(&car, &saved, sizeof(car)) == 0);
	car.position[1] = 1e7f;
	CHECK(mh_camera_supercars_view(&car, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved, sizeof(output)) == 0);
	CHECK(mh_camera_supercars_view(NULL, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved, sizeof(output)) == 0);
	CHECK(mh_camera_supercars_view(&car, NULL, NULL) != MH_ERROR_NONE);
	memset(&prior, 0, sizeof(prior));
	prior.rotation[0] = prior.rotation[4] = prior.rotation[8] = 1;
	CHECK(mh_camera_goal_view(&prior, eye, target, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.position[1] == 2 && output.position[2] == -4.5f);
	saved = output;
	CHECK(mh_camera_goal_view(&prior, eye, target, &prior, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&prior, &saved, sizeof(prior)) == 0);
	CHECK(mh_camera_goal_view(&prior, eye, eye, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(output.rotation, prior.rotation, 6 * sizeof(float)) == 0);
	CHECK(output.rotation[6] == 0 && output.rotation[7] == 0 &&
		output.rotation[8] == 0);
	saved = output;
	CHECK(mh_camera_goal_view(NULL, eye, target, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	eye[0] = 1000001;
	CHECK(mh_camera_goal_view(&prior, eye, target, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	eye[0] = 0;

	memset(&goal_track, 0, sizeof(goal_track));
	goal_track.has_goal_camera = 1;
	goal_track.goal_camera[0] = -6;
	goal_track.goal_camera[1] = 4;
	goal_track.goal_camera[2] = 66;
	goal_track.goal_camera[3] = 7;
	goal_track.goal_camera[5] = -2;
	memset(&view_field, 0, sizeof(view_field));
	view_field.listener_mask = 1;
	view_field.count = 2;
	view_field.active_mask = 3;
	view_field.cars[0].track = view_field.cars[1].track = &goal_track;
	view_field.cars[0].camera = prior;
	view_field.cars[0].camera_mode = MH_CAMERA_IN_CAR;
	view_field.cars[0].camera_focal = 3;
	view_field.cars[0].camera_far = 125;
	view_field.cars[0].race.finished = 1;
	CHECK(mh_field_camera_finish(&view_field, 0, NULL) == MH_ERROR_NONE);
	CHECK(view_field.cars[0].camera_goal &&
		!mh_field_listener_positional(&view_field) &&
		view_field.listener_revision == 0);
	CHECK(!mh_drive_camera_in_car(&view_field.cars[0]));
	CHECK(mh_field_camera_update(&view_field, .02f, NULL) == MH_ERROR_NONE);
	CHECK(view_field.cars[0].eye[0] == -6 &&
		view_field.cars[0].eye[2] == 66);
	CHECK(view_field.cars[0].camera_focal == 3 &&
		mh_drive_camera_far(&view_field.cars[0]) == 125);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_IN_CAR, NULL) ==
		MH_ERROR_NONE);
	CHECK(!view_field.cars[0].camera_goal &&
		view_field.listener_revision == 1);
	CHECK(mh_field_camera_finish(&view_field, 0, NULL) == MH_ERROR_NONE &&
		view_field.cars[0].camera_goal);

	/* Automatic reassertion is silent. */
	CHECK(view_field.listener_revision == 1);
	CHECK(mh_field_cycle_view(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.viewed_entry == 1);
	CHECK(!view_field.cars[1].camera_goal &&
		view_field.cars[1].camera_mode == MH_CAMERA_TRACK);
	CHECK(mh_field_listener_positional(&view_field) &&
		view_field.listener_revision == 2);
	CHECK(mh_field_camera_finish(&view_field, 0, NULL) == MH_ERROR_NONE &&
		!view_field.cars[1].camera_goal);
	CHECK(mh_field_camera_finish(&view_field, 1, NULL) == MH_ERROR_NONE &&
		view_field.cars[1].camera_goal);
	CHECK(mh_field_listener_positional(&view_field) &&
		view_field.listener_revision == 2);
	saved_field = view_field;
	CHECK(mh_field_camera_finish(&view_field, 2, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view_field, &saved_field, sizeof(view_field)) == 0);

	mode = 99;
	CHECK(mh_camera_cycle(0, 3, 5, &mode, NULL) == MH_ERROR_NONE &&
		mode == 2);
	CHECK(mh_camera_cycle(2, 3, 5, &mode, NULL) == MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(0, 3, 1, &mode, NULL) == MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(0, 3, 0, &mode, NULL) != MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(3, 3, 7, &mode, NULL) != MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(0, 3, 8, &mode, NULL) != MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(0, 0, 1, &mode, NULL) != MH_ERROR_NONE &&
		mode == 0);
	CHECK(mh_camera_cycle(0, 9, 1, &mode, NULL) != MH_ERROR_NONE &&
		mode == 0);

	memset(&view_field, 0, sizeof(view_field));
	view_field.listener_mask = 1;
	view_field.count = 3;
	view_field.active_mask = 5;
	view_field.unavailable_mask = 4;
	view_field.cars[0].camera.position[0] = 123;
	view_field.cars[0].camera.rotation[0] = 1;
	view_field.cars[0].camera_mode = MH_CAMERA_TRACK;
	view_field.cars[0].camera_focal = 2;
	view_field.cars[0].camera_far = 200;
	view_field.cars[0].eye[0] = 123;
	view_field.cars[0].target[0] = 124;
	view_field.cars[0].forward[0] = 1;
	view_field.cars[0].look_back = 1;
	view_field.cars[0].camera_rear_view = 1;
	view_field.cars[2].camera_roll.external = 1;
	view_field.cars[2].camera_roll.supported_seconds = .25f;
	saved_field = view_field;
	CHECK(mh_field_cycle_view(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.viewed_entry == 2);
	CHECK(view_field.cars[2].camera.position[0] == 123 &&
		view_field.cars[2].camera.rotation[0] == 1);
	CHECK(view_field.cars[2].camera_mode == MH_CAMERA_TRACK &&
		view_field.cars[2].look_back &&
		view_field.cars[2].camera_rear_view);
	CHECK(view_field.cars[2].camera_focal == 2 &&
		view_field.cars[2].camera_far == 200);
	CHECK(view_field.cars[2].eye[0] == 123 &&
		view_field.cars[2].target[0] == 124);
	CHECK(view_field.cars[2].forward[0] == 1 &&
		view_field.cars[2].camera_roll.external);
	CHECK(view_field.cars[2].camera_roll.supported_seconds == .25f);
	CHECK(memcmp(&view_field.cars[0], &saved_field.cars[0],
		sizeof(view_field.cars[0])) == 0);
	CHECK(memcmp(&view_field.cars[2].vehicle, &saved_field.cars[2].vehicle,
		sizeof(view_field.cars[2].vehicle)) == 0);
	saved_field = view_field;
	CHECK(mh_field_select_view(&view_field, 1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view_field, &saved_field, sizeof(view_field)) == 0);
	CHECK(mh_field_cycle_view(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.viewed_entry == 0);
	CHECK(mh_field_select_view(&view_field, 2, NULL) == MH_ERROR_NONE);
	view_field.network = 1;
	view_field.cars[2].camera.position[0] = 456;
	CHECK(mh_field_set_active(&view_field, 1, NULL) == MH_ERROR_NONE);
	CHECK(view_field.viewed_entry == 0 &&
		view_field.cars[0].camera.position[0] == 456);
	CHECK(view_field.unavailable_mask == 0);
	view_field.listener_revision = 0;
	CHECK(mh_field_select_view(&view_field, 0, NULL) == MH_ERROR_NONE &&
		view_field.listener_revision == 0);
	CHECK(mh_field_cycle_view(&view_field, NULL) == MH_ERROR_NONE &&
		view_field.listener_revision == 1);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_CHASE, NULL) ==
			MH_ERROR_NONE &&
		view_field.listener_revision == 2);
	CHECK(mh_field_camera_mode(&view_field, MH_CAMERA_CHASE, NULL) ==
			MH_ERROR_NONE &&
		view_field.listener_revision == 3);
	saved_field = view_field;
	CHECK(mh_field_camera_mode(&view_field, (enum mh_camera_mode)99,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view_field, &saved_field, sizeof(view_field)) == 0);

	memset(keys, 0, sizeof(keys));
	motion.count = 2;
	motion.keys = keys;
	keys[0].selector = -6;
	keys[1].selector = -4;
	keys[0].anchor[0] = -1;
	keys[1].anchor[0] = 1;
	CHECK(mh_track_camera_select(&motion, target, &selected, &mode, NULL) ==
		MH_ERROR_NONE);

	/* Equal distance retains first key. */
	CHECK(selected == 0 && mode == 6);
	keys[1].anchor[0] = 0;
	CHECK(mh_track_camera_select(&motion, target, &selected, &mode, NULL) ==
		MH_ERROR_NONE);

	/* Unsigned low word, not signed %. */
	CHECK(selected == 1 && mode == 1);
	keys[0].anchor[0] = 1e7f;
	CHECK(mh_track_camera_select(&motion, target, &selected, &mode, NULL) !=
		MH_ERROR_NONE);
	CHECK(selected == 1 && mode == 1);
	motion.count = 0;
	CHECK(mh_track_camera_select(&motion, target, &selected, &mode, NULL) !=
		MH_ERROR_NONE);
	CHECK(selected == 1 && mode == 1);

	memset(&car, 0, sizeof(car));
	car.rotation[0] = car.rotation[4] = car.rotation[8] = 1;
	memset(keys, 0, sizeof(keys));
	motion.count = 1;
	keys[0].anchor[2] = -10;
	keys[0].focal = -2;
	keys[0].far_factor = 10;
	keys[0].distance_cap = 100;
	CHECK(mh_track_camera_view(&car, &car, &motion, 150, 1, &track_pose,
		&lens, NULL) == MH_ERROR_NONE);
	CHECK(track_pose.position[2] == -10 && track_pose.rotation[8] == 1);
	CHECK(lens.focal == 2 && lens.far_depth == 165);
	saved = track_pose;
	saved_lens = lens;
	CHECK(mh_track_camera_view(&car, &car, &motion, 150, 0, &track_pose,
		&lens, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &track_pose, sizeof(saved)) == 0);
	CHECK(memcmp(&saved_lens, &lens, sizeof(lens)) == 0);
	keys[0].selector = 1;
	keys[0].anchor[2] = 0;
	CHECK(mh_track_camera_view(&car, &car, &motion, 150, 1, &track_pose,
		&lens, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &track_pose, sizeof(saved)) == 0);
	CHECK(memcmp(&saved_lens, &lens, sizeof(lens)) == 0);
	keys[0].selector = 0;
	CHECK(mh_track_camera_view(&car, &car, &motion, 150, 1, &track_pose,
		&lens, NULL) == MH_ERROR_NONE);
	CHECK(track_pose.rotation[0] == 1 && track_pose.rotation[4] == 1);
	CHECK(track_pose.rotation[6] == 0 && track_pose.rotation[7] == 0 &&
		track_pose.rotation[8] == 0);
	prior = car;
	memcpy(prior.position, eye, sizeof(eye));
	CHECK(mh_camera_follow(&prior, &car, eye, target, 1.0f / 60, 0, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(output.position, eye, sizeof(eye)) == 0);

	/* Stored-float camera fixtures. */
	CHECK(output.rotation[7] == -.11043153f &&
		output.rotation[8] == .993883729f);
	saved = output;
	CHECK(mh_camera_follow(&prior, &car, eye, target, 1.0f / 60, 0, &prior,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&prior, &saved, sizeof(saved)) == 0);
	CHECK(mh_camera_follow(&prior, &car, eye, target, 1.0f / 60, 1, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.position[1] == 16.1451187f && output.position[2] == -12);
	CHECK(output.rotation[7] == -.755513906f &&
		output.rotation[8] == .655132651f);
	saved = output;
	CHECK(mh_camera_follow(NULL, &car, eye, target, 0, 0, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_camera_follow(&prior, &car, eye, target, -1, 0, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_camera_follow(&prior, &car, eye, target, 11, 0, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_camera_follow(&prior, &car, eye, target, 0, 2, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(saved)) == 0);
	prior.position[0] = 1e7f;
	CHECK(mh_camera_follow(&prior, &car, eye, target, 0, 0, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(saved)) == 0);

	/* No invented fallback basis for a zero look vector. */
	prior = car;
	memset(eye, 0, sizeof(eye));
	memset(target, 0, sizeof(target));
	CHECK(mh_camera_follow(&prior, &car, eye, target, 0, 0, &output,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 9; i++)
		CHECK(output.rotation[i] == 0);

	CHECK(mh_camera_lens_setup(1, .5f, 150, 1.5f, &lens, NULL) ==
		MH_ERROR_NONE);
	CHECK(lens.focal == 1 && lens.near_depth == .5f &&
		lens.far_depth == 150);
	CHECK(fabs(lens.pixel_aspect - 8.0 / 9) < 1e-7);
	CHECK(mh_camera_lens_setup(-1, -1, -1, 4.0f / 3, &lens, NULL) ==
		MH_ERROR_NONE);
	CHECK(lens.focal == .5f && lens.near_depth == .1f &&
		lens.far_depth == 60 && lens.pixel_aspect == 1);
	CHECK(mh_camera_lens_setup(100, 100, 1000, 4.0f / 3, &lens, NULL) ==
		MH_ERROR_NONE);
	CHECK(lens.focal == 10 && lens.near_depth == 10 &&
		lens.far_depth == 300);
	saved_lens = lens;
	CHECK(mh_camera_lens_setup(1, .5f, 150, 0, &lens, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&lens, &saved_lens, sizeof(lens)) == 0);
	CHECK(mh_camera_view(&prior, &car, 0, MH_CAMERA_IN_CAR, 0, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.position[0] == 0 && output.position[1] == 1 &&
		output.position[2] == 0);
	CHECK(memcmp(output.rotation, car.rotation, sizeof(car.rotation)) == 0);
	CHECK(mh_camera_view(&prior, &car, 0, MH_CAMERA_IN_CAR, 1, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.position[1] == 1.2f && output.position[2] == -1);
	CHECK(output.rotation[0] == -1 && output.rotation[8] == -1 &&
		output.rotation[6] == -8.74227766e-8f);
	CHECK(mh_camera_view(&prior, &car, 0, MH_CAMERA_CHASE, 1, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.position[1] == 2 && output.position[2] == 5);
	saved = output;
	CHECK(mh_camera_view(&prior, &car, 0, (enum mh_camera_mode)2, 1,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(mh_camera_view(&prior, &car, 0, MH_CAMERA_CHASE, 2, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(saved)) == 0);
	CHECK(mh_camera_view(&prior, &car, 0, MH_CAMERA_CHASE, 1, &car, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&car, &saved, sizeof(saved)) == 0);
	memset(&roll, 0, sizeof(roll));
	CHECK(mh_camera_roll_step(&roll, -1, supported, .25f, &roll, NULL) ==
		MH_ERROR_NONE);

	/* Entry does not accrue. */
	CHECK(roll.external && roll.supported_seconds == 0);
	CHECK(mh_camera_roll_step(&roll, 1, supported, .25f, &roll, NULL) ==
		MH_ERROR_NONE);
	CHECK(roll.external && roll.supported_seconds == .25f);
	supported[2] = 0;
	CHECK(mh_camera_roll_step(&roll, 1, supported, .5f, &roll, NULL) ==
		MH_ERROR_NONE);

	/* Two wheels hold. */
	CHECK(roll.external && roll.supported_seconds == .25f);
	supported[2] = 1;
	CHECK(mh_camera_roll_step(&roll, 1, supported, .25f, &roll, NULL) ==
		MH_ERROR_NONE);
	CHECK(roll.external && roll.supported_seconds == .5f);
	CHECK(mh_camera_roll_step(&roll, 1, supported, .01f, &roll, NULL) ==
			MH_ERROR_NONE &&
		!roll.external);
	saved_roll = roll;
	supported[0] = 2;
	CHECK(mh_camera_roll_step(&roll, 1, supported, 0, &roll, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&roll, &saved_roll, sizeof(roll)) == 0);

	return failures != 0;
}
