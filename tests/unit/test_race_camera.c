#include <math.h>
#include <stdio.h>
#include <string.h>

#include "client/race/internal.h"
#include "engine/render/scene_interpolation.h"
#include "engine/simulation/drive_internal.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "race camera line %d: %s\n", __LINE__, \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_track track;
static struct mh_race_loop loop;
static struct mh_field field, before, presented;

static int
frame(float elapsed)
{
	loop.elapsed = elapsed;
	CHECK(mh_race_remember_chase_camera(&loop));

	/* Inputs are sampled before the simulation takes its history copy. */
	before = field;
	CHECK(mh_drive_camera_update(loop.viewed, elapsed, &loop.error) ==
		MH_ERROR_NONE);
	CHECK(mh_race_update_chase_camera(&loop));
	CHECK(mh_scene_interpolate(&before, &field, .25, &presented, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

int
main(void)
{
	static const float intervals[] = {0, 1.0f / 120, 1.0f / 60, 1.0f / 30};
	struct mh_drive *drive;
	double forward, behind, angle;
	unsigned int rate, mode, repeat, i;

	for (rate = 0; rate < sizeof(intervals) / sizeof(intervals[0]);
		rate++) {
		for (mode = 0; mode < 2; mode++) {
			memset(&loop, 0, sizeof(loop));
			memset(&field, 0, sizeof(field));
			field.count = field.active_mask = 1;
			drive = &field.cars[0];
			drive->remote = 1;
			drive->track = &track;
			drive->camera_mode =
				mode ? MH_CAMERA_IN_CAR : MH_CAMERA_CHASE;
			drive->vehicle.motion.rotation[0] =
				drive->vehicle.motion.rotation[4] =
					drive->vehicle.motion.rotation[8] = 1;
			loop.field = &field;
			loop.drive = loop.viewed = drive;
			loop.continuous = 1;
			CHECK(mh_drive_camera_start(drive, NULL) ==
				MH_ERROR_NONE);

			for (repeat = 0; repeat < 3; repeat++) {
				drive->look_back = 1;
				CHECK(frame(intervals[rate]) == 0);
				forward = 0;

				for (i = 0; i < 3; i++)
					forward +=
						drive->camera.rotation[6 + i] *
						drive->vehicle.motion
							.rotation[6 + i];

				CHECK(forward < -.99);
				CHECK(memcmp(&presented.cars[0].camera,
					&drive->camera,
					sizeof(drive->camera)) == 0);

				/*
				 * Releasing after turning must use the car's
				 * current pose.
				 */
				angle = (repeat + 1) * .6;
				drive->vehicle.motion.rotation[0] =
					drive->vehicle.motion.rotation[8] =
						cos(angle);
				drive->vehicle.motion.rotation[2] = -sin(angle);
				drive->vehicle.motion.rotation[6] = sin(angle);
				drive->vehicle.motion.position[0] += 2;
				drive->look_back = 0;
				CHECK(frame(intervals[rate]) == 0);
				forward = behind = 0;

				for (i = 0; i < 3; i++) {
					forward +=
						drive->camera.rotation[6 + i] *
						drive->vehicle.motion
							.rotation[6 + i];
					behind +=
						(drive->camera.position[i] -
							drive->vehicle.motion
								.position[i]) *
						drive->vehicle.motion
							.rotation[6 + i];
				}

				CHECK(forward > .99);
				CHECK(mode || behind < -4);
				CHECK(memcmp(&presented.cars[0].camera,
					&drive->camera,
					sizeof(drive->camera)) == 0);
				CHECK(frame(intervals[rate]) == 0);
				CHECK(mode || loop.follow_active);
			}
		}
	}

	return 0;
}
