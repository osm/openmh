#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/camera_internal.h"

#define RACER_LIMIT 8
#define VALUE_LIMIT 1e6f
#define BASIS_LIMIT 10
#define FRAME_LIMIT 10
#define OFFSET_LIMIT 1000
#define ROLL_TIME_LIMIT 100
#define ROLL_MIN_SUPPORT 3
#define ROLL_RELEASE_SECONDS .5f
#define FOCAL_MINIMUM .5f
#define FOCAL_MAXIMUM 10
#define NEAR_THRESHOLD .1
#define NEAR_MINIMUM .1f
#define NEAR_MAXIMUM 10
#define FAR_MINIMUM 60
#define FAR_MAXIMUM 300
#define ASPECT_MINIMUM .01f
#define ASPECT_MAXIMUM 100
#define REFERENCE_ASPECT 1.3333333333333335
#define SUPERCARS_FORWARD 12
#define SUPERCARS_HEIGHT 25
#define REAR_VIEW_ANGLE 3.1415927410125732421875f
#define FOLLOW_REFERENCE_RATE 60
#define FOLLOW_HORIZONTAL_RATE .15
#define FOLLOW_VERTICAL_RATE .9
#define OVERHEAD_FORWARD -12
#define OVERHEAD_HEIGHT 17
#define PITCH_HEIGHT_SCALE 3
#define FOLLOW_WEIGHT_MINIMUM .05f
#define FOLLOW_WEIGHT_MAXIMUM .9f

enum mh_error_code
mh_camera_cycle(unsigned int current, unsigned int count,
	unsigned int active_mask, unsigned int *selected,
	struct mh_error *error_out)
{
	unsigned int next, tries;

	if (selected == NULL || count < 1 || count > RACER_LIMIT ||
		current >= count || active_mask == 0 ||
		active_mask >= (1U << count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera membership");

	next = current;

	for (tries = 0; tries < count; tries++) {
		next = (next + 1) % count;

		if (active_mask & (1U << next)) {
			*selected = next;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "empty camera membership");
}

enum mh_error_code
mh_camera_roll_step(const struct mh_camera_roll *state, float up_y,
	const int supported[4], float elapsed_seconds,
	struct mh_camera_roll *output, struct mh_error *error_out)
{
	struct mh_camera_roll result;
	unsigned int wheel, supported_count;

	if (state == NULL || supported == NULL || output == NULL ||
		(state->external != 0 && state->external != 1) ||
		!(state->supported_seconds >= 0 &&
			state->supported_seconds <= ROLL_TIME_LIMIT) ||
		!(up_y >= -BASIS_LIMIT && up_y <= BASIS_LIMIT) ||
		!(elapsed_seconds >= 0 && elapsed_seconds <= FRAME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera roll state");

	supported_count = 0;

	for (wheel = 0; wheel < 4; wheel++) {
		if (supported[wheel] != 0 && supported[wheel] != 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid camera support");

		supported_count += supported[wheel] != 0;
	}

	result = *state;

	if (result.external) {
		if (supported_count >= ROLL_MIN_SUPPORT)
			result.supported_seconds =
				(float)((long double)result.supported_seconds +
					elapsed_seconds);

		if (result.supported_seconds > ROLL_RELEASE_SECONDS)
			result.external = 0;
	} else if (up_y < 0) {
		result.external = 1;
		result.supported_seconds = 0;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_camera_lens_setup(float focal, float near_depth, float far_depth,
	float aspect, struct mh_camera_lens *output, struct mh_error *error_out)
{
	struct mh_camera_lens result;

	if (output == NULL ||
		!(focal >= -VALUE_LIMIT && focal <= VALUE_LIMIT) ||
		!(near_depth >= -VALUE_LIMIT && near_depth <= VALUE_LIMIT) ||
		!(far_depth >= -VALUE_LIMIT && far_depth <= VALUE_LIMIT) ||
		!(aspect >= ASPECT_MINIMUM && aspect <= ASPECT_MAXIMUM))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid camera lens");

	result.focal = focal < FOCAL_MINIMUM
		? FOCAL_MINIMUM
		: (focal > FOCAL_MAXIMUM ? FOCAL_MAXIMUM : focal);
	result.near_depth = near_depth < NEAR_THRESHOLD
		? NEAR_MINIMUM
		: (near_depth > NEAR_MAXIMUM ? NEAR_MAXIMUM : near_depth);
	result.far_depth = far_depth < FAR_MINIMUM
		? FAR_MINIMUM
		: (far_depth > FAR_MAXIMUM ? FAR_MAXIMUM : far_depth);
	result.pixel_aspect = (float)((long double)REFERENCE_ASPECT / aspect);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * C89 has no sqrtl. Refine the binary64 library seed in the available long
 * double precision before the original explicit float/double stores.
 */
long double
mh_camera_sqrt(long double value)
{
	long double result;

	if (value == 0)
		return 0;

	result = sqrt((double)value);
	result = (result + value / result) * .5L;

	return (result + value / result) * .5L;
}

enum mh_error_code
mh_camera_supercars_view(const struct mh_camera_pose *car,
	struct mh_camera_pose *output, struct mh_error *error_out)
{
	static const float basis[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
	struct mh_camera_pose result;
	unsigned int i;

	if (car == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing Supercars camera pose");

	for (i = 0; i < 9; i++) {
		if (!(car->rotation[i] >= -BASIS_LIMIT &&
			car->rotation[i] <= BASIS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Supercars car rotation");

		result.rotation[i] = basis[i];
	}

	for (i = 0; i < 3; i++)
		if (!(car->position[i] >= -VALUE_LIMIT &&
			car->position[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Supercars car position");

	result.position[0] =
		(float)((long double)car->rotation[6] * SUPERCARS_FORWARD +
			car->position[0]);
	result.position[1] =
		(float)((long double)car->position[1] + SUPERCARS_HEIGHT);
	result.position[2] =
		(float)((long double)car->rotation[8] * SUPERCARS_FORWARD +
			car->position[2]);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

long double
mh_camera_length_squared(const float vector[3])
{
	return (long double)vector[1] * vector[1] +
		(long double)vector[0] * vector[0] +
		(long double)vector[2] * vector[2];
}

static void
normalize_vector(float vector[3])
{
	double length_squared;
	long double factor;
	unsigned int axis;

	length_squared = (double)mh_camera_length_squared(vector);

	if (length_squared == 0)
		return;

	factor = 1 / mh_camera_sqrt(length_squared);

	for (axis = 0; axis < 3; axis++)
		vector[axis] = (float)((long double)vector[axis] * factor);
}

float
mh_camera_rotate_axis(
	const float rotation[9], const float vector[3], unsigned int axis)
{
	return (float)((long double)vector[1] * rotation[3 + axis] +
		(long double)vector[0] * rotation[axis] +
		(long double)vector[2] * rotation[6 + axis]);
}

void
mh_camera_look_at(struct mh_camera_pose *pose, const float target[3],
	int keep_vertical_up)
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		pose->rotation[6 + i] =
			(float)((long double)target[i] - pose->position[i]);

	if (mh_camera_length_squared(pose->rotation + 6) == 0)
		return;

	normalize_vector(pose->rotation + 6);
	pose->rotation[0] = pose->rotation[8];
	pose->rotation[1] = 0;
	pose->rotation[2] = -pose->rotation[6];

	if (keep_vertical_up && mh_camera_length_squared(pose->rotation) == 0)
		return;

	normalize_vector(pose->rotation);

	for (i = 0; i < 3; i++)
		pose->rotation[3 + i] =
			(float)((long double)pose->rotation[6 + (i + 1) % 3] *
					pose->rotation[(i + 2) % 3] -
				(long double)pose->rotation[6 + (i + 2) % 3] *
					pose->rotation[(i + 1) % 3]);
}

static enum mh_error_code
world_target_view(const struct mh_camera_pose *previous, const float eye[3],
	const float target[3], int keep_vertical_up,
	struct mh_camera_pose *output, struct mh_error *error_out)
{
	struct mh_camera_pose result;
	unsigned int i;

	if (previous == NULL || eye == NULL || target == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid goal camera");

	for (i = 0; i < 3; i++)
		if (!(eye[i] >= -VALUE_LIMIT && eye[i] <= VALUE_LIMIT) ||
			!(target[i] >= -VALUE_LIMIT &&
				target[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid goal camera coordinates");

	for (i = 0; i < 9; i++)
		if (!(previous->rotation[i] >= -VALUE_LIMIT &&
			previous->rotation[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid prior goal camera basis");

	result = *previous;

	for (i = 0; i < 3; i++)
		result.position[i] = eye[i];

	mh_camera_look_at(&result, target, keep_vertical_up);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_camera_goal_view(const struct mh_camera_pose *previous, const float eye[3],
	const float target[3], struct mh_camera_pose *output,
	struct mh_error *error_out)
{
	return world_target_view(previous, eye, target, 0, output, error_out);
}

enum mh_error_code
mh_camera_intro_view(const struct mh_camera_pose *previous, const float eye[3],
	const float target[3], struct mh_camera_pose *output,
	struct mh_error *error_out)
{
	return world_target_view(previous, eye, target, 1, output, error_out);
}

enum mh_error_code
mh_camera_follow(const struct mh_camera_pose *previous,
	const struct mh_camera_pose *car, const float eye_offset[3],
	const float target_offset[3], float elapsed_seconds, int overhead,
	struct mh_camera_pose *output, struct mh_error *error_out)
{
	static const float overhead_target[3] = {0, 0, 2};
	struct mh_camera_pose result;
	float weights[3], offset[3], direction[3], prior[3], desired[3],
		target[3], distance;
	long double step, value;
	unsigned int i;

	if (previous == NULL || car == NULL || eye_offset == NULL ||
		target_offset == NULL || output == NULL ||
		!(elapsed_seconds >= 0 && elapsed_seconds <= FRAME_LIMIT) ||
		(overhead != 0 && overhead != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid camera input");

	for (i = 0; i < 3; i++) {
		if (!(previous->position[i] >= -VALUE_LIMIT &&
			previous->position[i] <= VALUE_LIMIT) ||
			!(car->position[i] >= -VALUE_LIMIT &&
				car->position[i] <= VALUE_LIMIT) ||
			!(eye_offset[i] >= -OFFSET_LIMIT &&
				eye_offset[i] <= OFFSET_LIMIT) ||
			!(target_offset[i] >= -OFFSET_LIMIT &&
				target_offset[i] <= OFFSET_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid camera position");
	}

	for (i = 0; i < 9; i++)
		if (!(car->rotation[i] >= -BASIS_LIMIT &&
			car->rotation[i] <= BASIS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid camera car rotation");

	if (overhead) {
		weights[0] = FOLLOW_WEIGHT_MINIMUM;
		weights[1] = FOLLOW_WEIGHT_MAXIMUM;
		weights[2] = 1;
		offset[0] = (float)((long double)car->rotation[6] *
			OVERHEAD_FORWARD);
		offset[1] = OVERHEAD_HEIGHT;
		offset[2] = (float)((long double)car->rotation[8] *
			OVERHEAD_FORWARD);
	} else {
		step = mh_camera_sqrt(
			(long double)elapsed_seconds * FOLLOW_REFERENCE_RATE);
		weights[0] = weights[2] =
			(float)(step * (long double)FOLLOW_HORIZONTAL_RATE);
		weights[1] = (float)(step * (long double)FOLLOW_VERTICAL_RATE);

		for (i = 0; i < 3; i++) {
			if (weights[i] < FOLLOW_WEIGHT_MINIMUM)
				weights[i] = FOLLOW_WEIGHT_MINIMUM;

			if (weights[i] > FOLLOW_WEIGHT_MAXIMUM)
				weights[i] = FOLLOW_WEIGHT_MAXIMUM;

			offset[i] = eye_offset[i];
		}
	}

	distance = (float)mh_camera_sqrt(mh_camera_length_squared(offset));

	for (i = 0; i < 3; i++)
		direction[i] = (float)((long double)previous->position[i] -
			car->position[i]);

	normalize_vector(direction);

	for (i = 0; i < 3; i++) {
		direction[i] = (float)((long double)direction[i] * distance);
		prior[i] =
			(float)((long double)car->position[i] + direction[i]);

		if (overhead) {
			desired[i] = (float)((long double)offset[i] +
				car->position[i]);
			target[i] = mh_camera_rotate_axis(
				car->rotation, overhead_target, i);
		} else {
			value = (long double)offset[1] * car->rotation[3 + i] +
				(long double)offset[0] * car->rotation[i] +
				(long double)offset[2] * car->rotation[6 + i] +
				car->position[i];
			desired[i] = (float)value;
			target[i] = target_offset[i];
		}
	}

	desired[1] = (float)((long double)offset[1] + car->position[1] -
		(long double)car->rotation[7] * PITCH_HEIGHT_SCALE);

	for (i = 0; i < 3; i++) {
		result.position[i] =
			(float)(((long double)desired[i] - prior[i]) *
					weights[i] +
				prior[i]);
		result.rotation[6 + i] = (float)((long double)car->position[i] +
			target[i] - result.position[i]);
	}

	normalize_vector(result.rotation + 6);
	result.rotation[0] = result.rotation[8];
	result.rotation[1] = 0;
	result.rotation[2] = -result.rotation[6];
	normalize_vector(result.rotation);

	for (i = 0; i < 3; i++)
		result.rotation[3 + i] =
			(float)((long double)result.rotation[6 + (i + 1) % 3] *
					result.rotation[(i + 2) % 3] -
				(long double)result.rotation[6 + (i + 2) % 3] *
					result.rotation[(i + 1) % 3]);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_camera_view(const struct mh_camera_pose *previous,
	const struct mh_camera_pose *car, float elapsed_seconds,
	enum mh_camera_mode mode, int look_back, struct mh_camera_pose *output,
	struct mh_error *error_out)
{
	static const float eye_offset[3] = {0, 2, -4.5f};
	static const float target_offset[3] = {0, 1.5f, 0};
	struct mh_camera_pose result;
	float height_offset, forward_offset;
	long double cosine, sine;
	unsigned int i;

	if (previous == NULL || car == NULL || output == NULL ||
		(mode != MH_CAMERA_CHASE && mode != MH_CAMERA_IN_CAR) ||
		(look_back != 0 && look_back != 1) ||
		!(elapsed_seconds >= 0 && elapsed_seconds <= FRAME_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid camera view");

	if (mode == MH_CAMERA_CHASE && !look_back)
		return mh_camera_follow(previous, car, eye_offset,
			target_offset, elapsed_seconds, 0, output, error_out);

	for (i = 0; i < 9; i++)
		if (!(car->rotation[i] >= -BASIS_LIMIT &&
			car->rotation[i] <= BASIS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attached camera rotation");

	for (i = 0; i < 3; i++)
		if (!(car->position[i] >= -VALUE_LIMIT &&
			car->position[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attached camera position");

	result = *car;
	height_offset = mode == MH_CAMERA_IN_CAR ? (look_back ? 1.2f : 1) : 2;
	forward_offset = mode == MH_CAMERA_IN_CAR ? (look_back ? -1 : 0) : 5;

	for (i = 0; i < 3; i++) {
		result.position[i] = (float)((long double)height_offset *
				car->rotation[3 + i] +
			result.position[i]);
		result.position[i] = (float)((long double)forward_offset *
				car->rotation[6 + i] +
			result.position[i]);
	}

	if (look_back) {
		/*
		 * The caller passes binary32 pi, not exact pi: preserve its
		 * small sine component instead of just negating the X/Z basis
		 * columns.
		 */
		cosine = cos((double)REAR_VIEW_ANGLE);
		sine = sin((double)REAR_VIEW_ANGLE);

		for (i = 0; i < 3; i++) {
			result.rotation[i] = (float)(cosine * car->rotation[i] -
				sine * car->rotation[6 + i]);
			result.rotation[6 + i] =
				(float)(cosine * car->rotation[6 + i] +
					sine * car->rotation[i]);
		}
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_camera_follow_motion(const struct mh_camera_pose *previous,
	const float previous_car[3], const struct mh_camera_pose *car,
	float elapsed_seconds, struct mh_camera_pose *output,
	struct mh_error *error_out)
{
	struct mh_camera_pose carried;
	unsigned int i;

	if (previous == NULL || previous_car == NULL || car == NULL ||
		output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing chase motion");

	carried = *previous;

	for (i = 0; i < 3; i++) {
		if (!(previous_car[i] >= -VALUE_LIMIT &&
			previous_car[i] <= VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid previous chase car");

		carried.position[i] =
			(float)((long double)previous->position[i] +
				car->position[i] - previous_car[i]);
	}

	return mh_camera_view(&carried, car, elapsed_seconds, MH_CAMERA_CHASE,
		0, output, error_out);
}
