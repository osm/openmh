#include <math.h>
#include <stddef.h>

#include "engine/audio/sound_spatial.h"
#include "engine/core/error.h"
#include "engine/simulation/camera.h"
#include "engine/simulation/motion.h"

#define LISTENER_AXIS_LIMIT 1.000001
#define UPRIGHT_DIRECTION_EPSILON 1e-10

enum mh_error_code
mh_sound_vehicle_spatial_prepare(const struct mh_sound_listener *listener,
	const struct mh_motion_state *motion, double volume,
	double near_distance, double far_distance,
	struct mh_sound_spatial_result *output, struct mh_error *error_out)
{
	double position[3], velocity[3];
	enum mh_error_code status;

	status = mh_motion_display(motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_sound_spatial_prepare(listener, position, volume,
		near_distance, far_distance, output, error_out);
}

void
mh_sound_listener_pose(
	const struct mh_camera_pose *pose, struct mh_sound_listener *listener)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		listener->position[i] = pose->position[i];
		listener->right[i] = pose->rotation[i];
		listener->forward[i] = pose->rotation[6 + i];
	}
}

void
mh_sound_listener_camera(const double eye[3], const double target[3],
	struct mh_sound_listener *listener)
{
	double length, horizontal;
	unsigned int i;

	length = 0;

	for (i = 0; i < 3; i++) {
		listener->position[i] = eye[i];
		listener->forward[i] = target[i] - eye[i];
		length += listener->forward[i] * listener->forward[i];
	}

	length = sqrt(length);

	if (length > UPRIGHT_DIRECTION_EPSILON)
		for (i = 0; i < 3; i++)
			listener->forward[i] /= length;
	else {
		listener->forward[0] = listener->forward[1] = 0;
		listener->forward[2] = 1;
	}

	horizontal = sqrt(listener->forward[0] * listener->forward[0] +
		listener->forward[2] * listener->forward[2]);
	listener->right[1] = 0;

	if (horizontal > UPRIGHT_DIRECTION_EPSILON) {
		listener->right[0] = listener->forward[2] / horizontal;
		listener->right[2] = -listener->forward[0] / horizontal;
	} else {
		listener->right[0] = 1;
		listener->right[2] = 0;
	}
}

/* Recover long double precision from the C89 double square root. */
static long double
refined_sqrt(long double value)
{
	long double result;

	if (value == 0)
		return 0;

	result = sqrt((double)value);
	result = (result + value / result) * .5L;

	return (result + value / result) * .5L;
}

enum mh_error_code
mh_sound_spatial_prepare(const struct mh_sound_listener *listener,
	const double source[3], double volume, double near_distance,
	double far_distance, struct mh_sound_spatial_result *output,
	struct mh_error *error_out)
{
	struct mh_sound_spatial_result next;
	float delta[3], distance, x, z, attenuation, interval, near_value,
		far_value;
	long double length, front_weight, side_weight;
	double left, right;
	unsigned int i;

	if (listener == NULL || source == NULL || output == NULL ||
		!(volume >= 0 && volume <= 100) ||
		!(near_distance >= 0 && near_distance < far_distance &&
			far_distance <= MH_SOUND_DISTANCE_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid positional sound parameters");

	for (i = 0; i < 3; i++) {
		if (!(source[i] >= -MH_SOUND_POSITION_LIMIT &&
			source[i] <= MH_SOUND_POSITION_LIMIT) ||
			!(listener->position[i] >= -MH_SOUND_POSITION_LIMIT &&
				listener->position[i] <=
					MH_SOUND_POSITION_LIMIT) ||
			!(listener->right[i] >= -LISTENER_AXIS_LIMIT &&
				listener->right[i] <= LISTENER_AXIS_LIMIT) ||
			!(listener->forward[i] >= -LISTENER_AXIS_LIMIT &&
				listener->forward[i] <= LISTENER_AXIS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid positional sound transform");

		delta[i] = (float)((long double)(float)source[i] -
			(float)listener->position[i]);
	}

	distance =
		(float)refined_sqrt(((long double)delta[1] * delta[1] +
					(long double)delta[0] * delta[0]) +
			(long double)delta[2] * delta[2]);
	near_value = (float)near_distance;
	far_value = (float)far_distance;
	interval = (float)((long double)far_value - near_value);
	next.gains[0] = next.gains[1] = 0;
	next.eligible = 0;

	if (interval <= 0 || distance > far_value) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	attenuation = distance < near_value
		? 1
		: (float)(1 - ((long double)distance - near_value) / interval);

	if (attenuation <= 0) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	next.eligible = 1;
	x = (float)(((long double)delta[1] * (float)listener->right[1] +
			(long double)delta[0] * (float)listener->right[0]) +
		(long double)delta[2] * (float)listener->right[2]);
	z = (float)(((long double)delta[1] * (float)listener->forward[1] +
			(long double)delta[0] *
				(float)listener->forward[0]) +
		(long double)delta[2] * (float)listener->forward[2]);

	/* Store the squared horizontal length as binary64 first. */
	length =
		refined_sqrt((double)((long double)x * x + (long double)z * z));

	if (length > 0) {
		length = 1 / length;
		x = (float)(x * length);
		z = (float)(z * length);
	}

	front_weight = (long double)z * .2 + .8;
	side_weight = (long double)x * .4;
	left = (double)(float)(((.6 - side_weight) * front_weight *
				attenuation) *
		(float)volume);
	right = (double)(float)(((.6 + side_weight) * front_weight *
					attenuation) *
		(float)volume);
	next.gains[0] = left < 0 ? 0 : left > 1 ? 1 : left;
	next.gains[1] = right < 0 ? 0 : right > 1 ? 1 : right;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sound_spatial(const struct mh_sound_listener *listener,
	const double source[3], double volume, double near_distance,
	double far_distance, double gains[2], struct mh_error *error_out)
{
	struct mh_sound_spatial_result result = {{0, 0}, 0};
	enum mh_error_code status;

	if (gains == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing positional sound output");

	status = mh_sound_spatial_prepare(listener, source, volume,
		near_distance, far_distance, &result, error_out);

	if (status == MH_ERROR_NONE) {
		gains[0] = result.gains[0];
		gains[1] = result.gains[1];
	}

	return status;
}
