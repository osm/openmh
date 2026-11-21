#include <math.h>

#include "engine/core/pose.h"
#include "engine/render/scene_interpolation.h"

#define SNAP_DISTANCE 16
#define WHEEL_WRAP 125.6636962890625

static double
blend(double before, double after, double alpha)
{
	return before + (after - before) * alpha;
}

static int
nearby(const struct mh_motion_state *before,
	const struct mh_motion_state *after)
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (fabs(before->position[i] - after->position[i]) >
			SNAP_DISTANCE)
			return 0;

	return 1;
}

static void
motion(const struct mh_motion_state *before, struct mh_motion_state *after,
	double alpha)
{
	unsigned int i;

	if (!nearby(before, after))
		return;

	if (mh_pose_blend(before->rotation, after->rotation, alpha,
		after->rotation, NULL) != MH_ERROR_NONE)
		return;

	for (i = 0; i < 3; i++)
		after->position[i] =
			blend(before->position[i], after->position[i], alpha);

	for (i = 0; i < 4; i++)
		after->extension[i] =
			blend(before->extension[i], after->extension[i], alpha);
}

static void
camera(const struct mh_drive *before, struct mh_drive *after, double alpha)
{
	double a[9], b[9], result[9];
	unsigned int i;

	if (before->camera_mode != after->camera_mode ||
		before->camera_intro != after->camera_intro ||
		before->camera_goal != after->camera_goal ||
		before->look_back != after->look_back ||
		before->camera_rear_view != after->camera_rear_view)
		return;

	for (i = 0; i < 9; i++) {
		a[i] = before->camera.rotation[i];
		b[i] = after->camera.rotation[i];
	}

	if (mh_pose_blend(a, b, alpha, result, NULL) != MH_ERROR_NONE)
		return;

	for (i = 0; i < 9; i++)
		after->camera.rotation[i] = (float)result[i];

	for (i = 0; i < 3; i++) {
		after->camera.position[i] =
			(float)blend(before->camera.position[i],
				after->camera.position[i], alpha);
		after->eye[i] = blend(before->eye[i], after->eye[i], alpha);
		after->target[i] =
			blend(before->target[i], after->target[i], alpha);
		after->forward[i] =
			blend(before->forward[i], after->forward[i], alpha);
	}

	after->camera_focal =
		(float)blend(before->camera_focal, after->camera_focal, alpha);
	after->camera_far =
		(float)blend(before->camera_far, after->camera_far, alpha);
}

enum mh_error_code
mh_scene_interpolate(const struct mh_field *previous,
	const struct mh_field *current, double alpha, struct mh_field *output,
	struct mh_error *error_out)
{
	const struct mh_drive *before;
	struct mh_drive *after;
	double angle;
	unsigned int i, j;

	if (previous == NULL || current == NULL || output == NULL ||
		output == previous || output == current ||
		!(alpha >= 0 && alpha <= 1) ||
		current->count > MH_RACE_MAX_CARS ||
		current->object_count > MH_FIELD_OBJECTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene interpolation");

	*output = *current;

	if (previous->count != current->count ||
		previous->object_count != current->object_count ||
		previous->cars[0].frame.fixed_ticks >
			current->cars[0].frame.fixed_ticks)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < current->count; i++) {
		before = &previous->cars[i];
		after = &output->cars[i];

		if (!(previous->active_mask & current->active_mask &
			(1U << i)) ||
			before->slot != after->slot ||
			before->car != after->car ||
			after->recovery_event != MH_RECOVERY_NONE ||
			!nearby(&before->vehicle.motion,
				&after->vehicle.motion))
			continue;

		motion(&before->vehicle.motion, &after->vehicle.motion, alpha);

		for (j = 0; j < 4; j++)
			after->remote_extension[j] =
				blend(before->remote_extension[j],
					after->remote_extension[j], alpha);

		after->vehicle.smoothed_steering =
			blend(before->vehicle.smoothed_steering,
				after->vehicle.smoothed_steering, alpha);

		for (j = 0; j < 2; j++) {
			angle = after->wheels.angle[j] -
				before->wheels.angle[j];

			/*
			 * Undo the producer's twenty-revolution wrap,
			 * preserving spin.
			 */
			if (angle > WHEEL_WRAP / 2)
				angle -= WHEEL_WRAP;

			if (angle < -WHEEL_WRAP / 2)
				angle += WHEEL_WRAP;

			after->wheels.angle[j] =
				before->wheels.angle[j] + alpha * angle;
		}

		if (previous->viewed_entry == current->viewed_entry)
			camera(before, after, alpha);
	}

	for (i = 0; i < current->object_count; i++)
		if (previous->objects[i].source.scene ==
			current->objects[i].source.scene)
			motion(&previous->objects[i].state.motion,
				&output->objects[i].state.motion, alpha);

	output->scene_seconds =
		blend(previous->scene_seconds, current->scene_seconds, alpha);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
