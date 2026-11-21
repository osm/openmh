#include <math.h>

#include "client/race/internal.h"
#include "engine/core/error.h"

#define FIXED_CHASE_HEIGHT 2
#define FIXED_CHASE_DISTANCE 4.5
#define FIXED_CHASE_TARGET_HEIGHT 1.5
#define FOLLOW_RECOVERY_DISTANCE 10
#define FOLLOW_ELAPSED_LIMIT .25

void
mh_race_restore_camera(struct mh_race_loop *loop)
{
	struct mh_drive *viewed;
	unsigned int entry;

	/*
	 * Captures retain their pre-advanced pose; scripted views own theirs.
	 */
	if (loop->capture != NULL || loop->field->attract ||
		(loop->session != NULL && loop->session->benchmark))
		return;

	entry = mh_field_viewed(loop->field);
	viewed = &loop->field->cars[entry];
	viewed->camera_mode = loop->camera_preference;
	viewed->camera_saved_in_car =
		loop->camera_preference == MH_CAMERA_IN_CAR;
	loop->field->listener_mask |= 1U << entry;
}

static void
publish_camera_pose(struct mh_drive *drive, const struct mh_camera_pose *pose)
{
	unsigned int i;

	drive->camera = *pose;

	for (i = 0; i < 3; i++) {
		drive->eye[i] = pose->position[i];
		drive->forward[i] = pose->rotation[6 + i];
		drive->target[i] = drive->eye[i] + drive->forward[i];
	}
}

/* Diagnostic fixed chase removes follow lag; it is not an original mode. */
static enum mh_error_code
fixed_chase(struct mh_drive *drive, struct mh_error *error_out)
{
	struct mh_camera_pose pose;
	double position[3], velocity[3];
	float eye[3], target[3];
	unsigned int i;
	enum mh_error_code status;

	if ((drive->cheats & (MH_CHEAT_IGNITION | MH_CHEAT_SUPERCARS)) ||
		drive->camera_mode != MH_CAMERA_CHASE || drive->look_back ||
		drive->camera_goal || drive->camera_intro)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_motion_display(
		&drive->vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++) {
		eye[i] = (float)(position[i] +
			FIXED_CHASE_HEIGHT *
				drive->vehicle.motion.rotation[3 + i] -
			FIXED_CHASE_DISTANCE *
				drive->vehicle.motion.rotation[6 + i]);
		target[i] = (float)(position[i] +
			(i == 1 ? FIXED_CHASE_TARGET_HEIGHT : 0));
	}

	status = mh_camera_goal_view(
		&drive->camera, eye, target, &pose, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	publish_camera_pose(drive, &pose);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_race_remember_chase_camera(struct mh_race_loop *loop)
{
	enum mh_error_code status;
	unsigned int i;

	loop->follow_active = loop->capture == NULL && !loop->debug_fixed &&
		(loop->continuous ||
			(loop->focused && !loop->paused &&
				!loop->screen.visible)) &&
		!(loop->viewed->cheats &
			(MH_CHEAT_IGNITION | MH_CHEAT_SUPERCARS)) &&
		loop->viewed->camera_mode == MH_CAMERA_CHASE &&
		!loop->viewed->look_back && !loop->viewed->camera_rear_view &&
		!loop->viewed->camera_goal && !loop->viewed->camera_intro;

	if (loop->follow_active) {
		loop->follow_previous = loop->viewed->camera;
		status = mh_motion_display(&loop->viewed->vehicle.motion,
			loop->follow_p, loop->follow_velocity, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Camera: %s\n", loop->error.message);

			loop->success = 0;
			loop->running = 0;

			return 0;
		}

		for (i = 0; i < 3; i++)
			loop->follow_position[i] = (float)loop->follow_p[i];
	}

	return 1;
}

int
mh_race_update_chase_camera(struct mh_race_loop *loop)
{
	enum mh_error_code status;
	unsigned int i;

	status = loop->error.code;

	if (loop->follow_active && status == MH_ERROR_NONE) {
		status = mh_motion_display(&loop->viewed->vehicle.motion,
			loop->follow_p, loop->follow_velocity, &loop->error);

		for (i = 0; i < 3; i++)
			loop->follow_car.position[i] = (float)loop->follow_p[i];

		for (i = 0; i < 9; i++)
			loop->follow_car.rotation[i] =
				(float)loop->viewed->vehicle.motion.rotation[i];

		/*
		 * A recovery/teleport keeps the original simulation
		 * follower.
		 */
		if (status == MH_ERROR_NONE &&
			fabs(loop->follow_car.position[0] -
				loop->follow_position[0]) <
				FOLLOW_RECOVERY_DISTANCE &&
			fabs(loop->follow_car.position[1] -
				loop->follow_position[1]) <
				FOLLOW_RECOVERY_DISTANCE &&
			fabs(loop->follow_car.position[2] -
				loop->follow_position[2]) <
				FOLLOW_RECOVERY_DISTANCE) {
			status = mh_camera_follow_motion(&loop->follow_previous,
				loop->follow_position, &loop->follow_car,
				loop->drive->frame.active
					? loop->drive->frame.time.elapsed
					: (float)(loop->elapsed > FOLLOW_ELAPSED_LIMIT
							? FOLLOW_ELAPSED_LIMIT
							: loop->elapsed),
				&loop->follow_result, &loop->error);

			if (status == MH_ERROR_NONE)
				publish_camera_pose(
					loop->viewed, &loop->follow_result);
		}

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Camera: %s\n", loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	if (loop->debug_fixed) {
		status = fixed_chase(loop->viewed, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Camera diagnostic: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	return 1;
}
