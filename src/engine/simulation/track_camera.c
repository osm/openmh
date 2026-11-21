#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/pose.h"
#include "engine/simulation/camera_internal.h"
#include "engine/simulation/track_camera.h"

#define KEY_LIMIT 100000
#define VALUE_LIMIT 1e6f
#define BASIS_LIMIT 10
#define FAR_MINIMUM 60
#define FAR_MAXIMUM 300
#define MODE_COUNT 7UL
#define DEGREES_PI 3.14159
#define DEGREES_SCALE .005555555555555555
#define ORBIT_ANGLE_SCALE .1
#define FAR_FACTOR_SCALE .01
#define FOCAL_MINIMUM .5
#define FOCAL_MAXIMUM 10

enum track_mode {
	FIXED_LOOK,
	INTERPOLATED_LOOK,
	FIXED_ANGLES,
	CAR_RELATIVE,
	WORLD_OFFSET,
	ORBIT,
	INTERPOLATED_ANGLES
};

enum mh_error_code
mh_track_camera_select(const struct mh_camera_motion *motion,
	const float position[3], size_t *index, unsigned int *mode,
	struct mh_error *error_out)
{
	size_t key_index, nearest;
	unsigned int axis;
	long double dx, dy, dz;
	float distance_squared, best_distance_squared;
	long selector;

	if (motion == NULL || motion->keys == NULL || motion->count == 0 ||
		motion->count > KEY_LIMIT || position == NULL ||
		index == NULL || mode == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track camera selection");

	for (axis = 0; axis < 3; axis++)
		if (!(position[axis] >= -VALUE_LIMIT &&
			position[axis] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track camera position");

	nearest = 0;
	best_distance_squared = 0;

	for (key_index = 0; key_index < motion->count; key_index++) {
		for (axis = 0; axis < 3; axis++)
			if (!(motion->keys[key_index].anchor[axis] >=
					-VALUE_LIMIT &&
				motion->keys[key_index].anchor[axis] <=
					VALUE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid track camera anchor");

		dx = (long double)motion->keys[key_index].anchor[0] -
			position[0];
		dy = (long double)motion->keys[key_index].anchor[1] -
			position[1];
		dz = (long double)motion->keys[key_index].anchor[2] -
			position[2];
		distance_squared = (float)((dx * dx + dy * dy) + dz * dz);

		if (key_index == 0 ||
			distance_squared < best_distance_squared) {
			best_distance_squared = distance_squared;
			nearest = key_index;
		}
	}

	if (!(motion->keys[nearest].selector >= -VALUE_LIMIT &&
		motion->keys[nearest].selector <= VALUE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track camera selector");

	selector = (long)((long double)motion->keys[nearest].selector + .5);
	*mode = (unsigned int)(((unsigned long)selector & MH_U32_MAX) %
		MODE_COUNT);
	*index = nearest;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
place_eye(const struct mh_camera_pose *car, const struct mh_camera_key *key,
	unsigned int mode, struct mh_camera_pose *result,
	struct mh_error *error_out)
{
	float direction[3], distance;
	long double length, factor, angle;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		direction[i] =
			(float)((long double)car->position[i] - key->anchor[i]);
		result->position[i] = key->anchor[i];
	}

	length = mh_camera_sqrt(mh_camera_length_squared(direction));
	distance = (float)length;

	if (mode == INTERPOLATED_LOOK || mode == INTERPOLATED_ANGLES) {
		if (length == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"coincident interpolated camera anchor");

		factor = 1 / length;

		for (i = 0; i < 3; i++)
			direction[i] =
				(float)((long double)direction[i] * factor);

		factor = (long double)key->parameter[0] * distance;
		result->position[0] =
			(float)(factor * direction[0] + key->anchor[0]);
		result->position[1] = (float)((long double)key->parameter[1] *
				factor * direction[1] +
			key->anchor[1]);
		result->position[2] =
			(float)(factor * direction[2] + key->anchor[2]);
	} else if (mode == CAR_RELATIVE) {
		for (i = 0; i < 3; i++)
			result->position[i] =
				(float)((long double)car->position[i] +
					mh_camera_rotate_axis(car->rotation,
						key->parameter, i));
	} else if (mode == WORLD_OFFSET) {
		for (i = 0; i < 3; i++)
			result->position[i] =
				(float)((long double)car->position[i] +
					key->parameter[i]);
	} else if (mode == ORBIT) {
		angle = (long double)key->parameter[0] * distance *
			(long double)ORBIT_ANGLE_SCALE;
		result->position[0] = (float)((long double)sin((double)angle) *
				key->parameter[1] +
			car->position[0]);
		result->position[1] = (float)((long double)car->position[1] +
			key->parameter[2]);
		result->position[2] = (float)((long double)cos((double)angle) *
				key->parameter[1] +
			car->position[2]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
orient_view(const struct mh_camera_pose *car, const struct mh_camera_key *key,
	unsigned int mode, struct mh_camera_pose *result,
	struct mh_error *error_out)
{
	const float *parameters;
	double rotation[9], angles[3];
	unsigned int i, offset;
	enum mh_error_code status;

	if (mode == FIXED_ANGLES || mode == CAR_RELATIVE ||
		mode == INTERPOLATED_ANGLES) {
		for (i = 0; i < 3; i++)
			angles[i] = 0;

		if (mode == CAR_RELATIVE) {
			for (i = 0; i < 9; i++)
				rotation[i] = car->rotation[i];

			status = mh_pose_angles(rotation, angles, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		offset = mode == FIXED_ANGLES ? 0 : 3;
		parameters = key->parameter + offset;

		for (i = 0; i < 3; i++)
			angles[i] = (double)(float)((long double)parameters[i] *
					(long double)DEGREES_PI *
					(long double)DEGREES_SCALE +
				angles[i]);

		status = mh_pose_matrix(angles, rotation, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 9; i++)
			result->rotation[i] = (float)rotation[i];
	} else
		mh_camera_look_at(result, car->position, 0);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
setup_lens(const struct mh_camera_pose *car, const struct mh_camera_key *key,
	const struct mh_camera_pose *view, float default_far, float aspect,
	struct mh_camera_lens *lens, struct mh_error *error_out)
{
	float direction[3], distance, zoom, far_depth;
	long double length, value;
	unsigned int i;

	for (i = 0; i < 3; i++)
		direction[i] = (float)((long double)car->position[i] -
			view->position[i]);

	length = mh_camera_sqrt(mh_camera_length_squared(direction));
	distance = (float)length;

	if (length > key->distance_cap)
		distance = key->distance_cap;

	value = (long double)distance * key->focal;
	zoom = (float)value;

	if (value < 1)
		zoom = 1;

	far_depth = (float)((long double)zoom * (long double)FAR_FACTOR_SCALE *
		key->far_factor);
	far_depth = (float)(1 + (long double)far_depth);
	far_depth = (float)((long double)default_far * far_depth);

	/*
	 * Clamp before the bounded public lens interface: valid records can
	 * intentionally request a far distance well above the 300-unit limit.
	 */
	if (far_depth < FAR_MINIMUM)
		far_depth = FAR_MINIMUM;

	if (far_depth > FAR_MAXIMUM)
		far_depth = FAR_MAXIMUM;

	value = key->focal < 0 ? -key->focal : zoom;

	if (value < FOCAL_MINIMUM)
		value = FOCAL_MINIMUM;

	if (value > FOCAL_MAXIMUM)
		value = FOCAL_MAXIMUM;

	return mh_camera_lens_setup((float)value, MH_CAMERA_RACE_NEAR,
		far_depth, aspect, lens, error_out);
}

enum mh_error_code
mh_track_camera_view(const struct mh_camera_pose *previous,
	const struct mh_camera_pose *car, const struct mh_camera_motion *motion,
	float default_far, float aspect, struct mh_camera_pose *output,
	struct mh_camera_lens *lens, struct mh_error *error_out)
{
	struct mh_camera_pose result;
	struct mh_camera_lens settings;
	const struct mh_camera_key *key;
	size_t index;
	unsigned int mode, i;
	enum mh_error_code status;

	if (previous == NULL || car == NULL || output == NULL || lens == NULL ||
		!(default_far >= FAR_MINIMUM && default_far <= FAR_MAXIMUM))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track camera view");

	status = mh_track_camera_select(
		motion, car->position, &index, &mode, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	key = &motion->keys[index];

	if (!(key->focal >= -VALUE_LIMIT && key->focal <= VALUE_LIMIT) ||
		!(key->far_factor >= -VALUE_LIMIT &&
			key->far_factor <= VALUE_LIMIT) ||
		!(key->distance_cap >= -VALUE_LIMIT &&
			key->distance_cap <= VALUE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track camera lens parameters");

	for (i = 0; i < 6; i++)
		if (!(key->parameter[i] >= -VALUE_LIMIT &&
			key->parameter[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track camera parameters");

	for (i = 0; i < 9; i++)
		if (!(previous->rotation[i] >= -BASIS_LIMIT &&
			previous->rotation[i] <= BASIS_LIMIT) ||
			!(car->rotation[i] >= -BASIS_LIMIT &&
				car->rotation[i] <= BASIS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track camera basis");

	result = *previous;

	status = place_eye(car, key, mode, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = orient_view(car, key, mode, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = setup_lens(
		car, key, &result, default_far, aspect, &settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;
	*lens = settings;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
