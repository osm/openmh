#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/track_camera.h"

#define VIEW_PERCENT_MIN 30
#define VIEW_PERCENT_MAX 150
#define VIEW_PERCENT_SCALE .01
#define FAR_MIN 60
#define FAR_MAX 300
#define CAMERA_TIME_LIMIT 10

static float
default_far(const struct mh_drive *drive)
{
	double distance;

	distance = drive->track->view_distance;

	if (!(distance > 0))
		distance = FAR_MAX;

	if (drive->view_percent >= VIEW_PERCENT_MIN &&
		drive->view_percent <= VIEW_PERCENT_MAX)
		distance = (float)((long double)(float)distance *
			((long double)drive->view_percent *
				(long double)VIEW_PERCENT_SCALE));

	if (distance < FAR_MIN)
		distance = FAR_MIN;

	if (distance > FAR_MAX)
		distance = FAR_MAX;

	return (float)distance;
}

static void
publish_view_vectors(struct mh_drive *drive, const struct mh_camera_pose *pose)
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++) {
		drive->eye[axis] = pose->position[axis];
		drive->forward[axis] = pose->rotation[6 + axis];
		drive->target[axis] = drive->eye[axis] + drive->forward[axis];
	}
}

static enum mh_error_code
update_camera(struct mh_drive *drive, float elapsed, int snap,
	struct mh_error *error_out)
{
	static const float eye_offset[3] = {0, 2, -4.5f};
	struct mh_camera_pose car, previous, result;
	struct mh_camera_roll roll;
	struct mh_camera_lens lens;
	enum mh_camera_mode mode;
	int look_back, ignition, supercars, hidden;
	double position[3], velocity[3];
	float goal_eye[3], goal_target[3];
	unsigned int i;
	enum mh_error_code status;

	if (drive == NULL || !(elapsed >= 0 && elapsed <= CAMERA_TIME_LIMIT) ||
		(drive->camera_mode != MH_CAMERA_CHASE &&
			drive->camera_mode != MH_CAMERA_IN_CAR &&
			drive->camera_mode != MH_CAMERA_TRACK) ||
		(drive->look_back != 0 && drive->look_back != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid camera session");

	ignition = (drive->cheats & MH_CHEAT_IGNITION) != 0;
	supercars = (drive->cheats & MH_CHEAT_SUPERCARS) != 0;
	hidden = ignition || supercars;

	/*
	 * Apply the viewed car's already updated inversion state. External
	 * requests clear only this car, even with a goal view active.
	 */
	mode = drive->camera_mode;
	look_back = drive->look_back;
	roll = drive->camera_roll;

	if (mode != MH_CAMERA_IN_CAR)
		memset(&roll, 0, sizeof(roll));
	else if (roll.external) {
		mode = MH_CAMERA_CHASE;
		look_back = 0;
	}

	if (drive->camera_goal && !snap && !hidden) {
		if (drive->track == NULL || !drive->track->has_goal_camera)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing goal camera");

		for (i = 0; i < 3; i++) {
			goal_eye[i] = (float)drive->track->goal_camera[i];
			goal_target[i] =
				(float)drive->track->goal_camera[3 + i];
		}

		status = mh_camera_goal_view(&drive->camera, goal_eye,
			goal_target, &result, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		drive->camera = result;
		drive->camera_rear_view = 0;
		drive->camera_roll = roll;
		drive->look_back = look_back;

		publish_view_vectors(drive, &result);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/* A rear-view pose must not seed the chase follower on release. */
	if (drive->camera_rear_view && !look_back && mode == MH_CAMERA_CHASE &&
		!hidden)
		snap = 1;

	status = mh_motion_display(
		&drive->vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	previous = drive->camera;

	for (i = 0; i < 9; i++)
		car.rotation[i] = (float)drive->vehicle.motion.rotation[i];

	for (i = 0; i < 3; i++) {
		car.position[i] = (float)position[i];

		/*
		 * Seed a new session or return from rear view behind the car.
		 * Relocation retains the shared pose.
		 */
		if (snap)
			previous.position[i] =
				(float)((long double)car.position[i] +
					(long double)eye_offset[1] *
						car.rotation[3 + i] +
					(long double)eye_offset[2] *
						car.rotation[6 + i]);
	}

	if (hidden)
		mode = MH_CAMERA_CHASE;

	status = mh_camera_lens_setup(1, MH_CAMERA_RACE_NEAR,
		default_far(drive), 1, &lens, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (supercars)
		status = mh_camera_supercars_view(&car, &result, error_out);
	else if (mode == MH_CAMERA_TRACK) {
		result = previous;

		if (drive->camera_motion != NULL &&
			drive->camera_motion->count != 0)
			status = mh_track_camera_view(&previous, &car,
				drive->camera_motion, default_far(drive), 1,
				&result, &lens, error_out);
	} else if (ignition && !look_back)
		status = mh_camera_follow(&previous, &car, eye_offset,
			eye_offset, elapsed, 1, &result, error_out);
	else
		status = mh_camera_view(&previous, &car, elapsed, mode,
			look_back, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	drive->camera = result;
	drive->camera_rear_view =
		look_back && mode != MH_CAMERA_TRACK && !supercars;
	drive->camera_roll = roll;
	drive->look_back = look_back;
	drive->camera_focal = lens.focal;
	drive->camera_far = lens.far_depth;

	/*
	 * Clear in-car, track, intro and goal requests each frame, including
	 * Supercars. Ctrl look-back and the explicit audio listener are
	 * retained.
	 */
	if (hidden) {
		drive->camera_mode = MH_CAMERA_CHASE;
		drive->camera_goal = 0;
		drive->camera_intro = 0;
	}

	publish_view_vectors(drive, &result);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_camera_update(
	struct mh_drive *drive, float elapsed, struct mh_error *error_out)
{
	return update_camera(drive, elapsed, 0, error_out);
}

enum mh_error_code
mh_drive_camera_roll_frame(
	struct mh_drive *drive, float elapsed, struct mh_error *error_out)
{
	int supported[4];
	unsigned int wheel;

	if (drive == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing camera inversion session");

	for (wheel = 0; wheel < 4; wheel++)
		supported[wheel] = drive->vehicle.grip.wheels[wheel].supported;

	return mh_camera_roll_step(&drive->camera_roll,
		(float)drive->vehicle.motion.rotation[4], supported, elapsed,
		&drive->camera_roll, error_out);
}

int
mh_drive_camera_in_car(const struct mh_drive *drive)
{
	return !(drive->cheats & (MH_CHEAT_IGNITION | MH_CHEAT_SUPERCARS)) &&
		!drive->camera_goal && drive->camera_mode == MH_CAMERA_IN_CAR &&
		!drive->camera_roll.external;
}

int
mh_drive_camera_look_back(const struct mh_drive *drive)
{
	if (drive->cheats & (MH_CHEAT_IGNITION | MH_CHEAT_SUPERCARS))
		return drive->look_back;

	return !drive->camera_goal && drive->look_back &&
		!drive->camera_roll.external;
}

enum mh_error_code
mh_drive_view_distance(
	struct mh_drive *drive, float percent, struct mh_error *error_out)
{
	if (drive == NULL || drive->track == NULL ||
		!(percent >= VIEW_PERCENT_MIN && percent <= VIEW_PERCENT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid view distance setting");

	drive->view_percent = percent;
	drive->camera_far = default_far(drive);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

float
mh_drive_camera_far(const struct mh_drive *drive)
{
	if ((drive->camera_goal || drive->camera_mode == MH_CAMERA_TRACK) &&
		drive->camera_far >= FAR_MIN && drive->camera_far <= FAR_MAX)
		return drive->camera_far;

	return default_far(drive);
}

enum mh_error_code
mh_drive_camera_start(struct mh_drive *drive, struct mh_error *error_out)
{
	return update_camera(drive, 0, 1, error_out);
}
