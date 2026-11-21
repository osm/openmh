#include <float.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/projection.h"

#define VALUE_LIMIT 1e9f
#define FACTOR_MIN 1e-6f
#define FACTOR_MAX 1e6f
#define VIEWPORT_LIMIT 16384
#define ASPECT_MIN .01f
#define ASPECT_MAX 100
#define ORIGINAL_ASPECT 1.3333333333333335

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

static int
valid_projection(const struct mh_projection *projection)
{
	unsigned int axis;

	if (projection == NULL ||
		!(projection->clip.near_distance >= FACTOR_MIN &&
			projection->clip.near_distance <= FACTOR_MAX) ||
		!(projection->far_distance > projection->clip.near_distance &&
			projection->far_distance <= VALUE_LIMIT) ||
		!(projection->inverse_far > 0 &&
			projection->inverse_far <= FACTOR_MAX) ||
		!(projection->inverse_focal > 0 &&
			projection->inverse_focal <= FACTOR_MAX) ||
		!(projection->inverse_half_width > 0 &&
			projection->inverse_half_width <= 2) ||
		!(projection->inverse_aspect > 0 &&
			projection->inverse_aspect <= FACTOR_MAX) ||
		!(projection->scale[0] > 0 &&
			projection->scale[0] <= VALUE_LIMIT) ||
		!(projection->scale[1] < 0 &&
			projection->scale[1] >= -VALUE_LIMIT))
		return 0;

	for (axis = 0; axis < 2; axis++)
		if (!bounded(projection->clip.center[axis]) ||
			!(projection->clip.extent[axis] >= 1 &&
				projection->clip.extent[axis] <=
					VIEWPORT_LIMIT))
			return 0;

	return 1;
}

enum mh_error_code
mh_projection_setup(unsigned int width, unsigned int height, float aspect,
	float focal, float near_distance, float far_distance,
	struct mh_projection *output, struct mh_error *error_out)
{
	struct mh_projection projection;
	long double ratio;
	double stored_ratio;

	if (output == NULL || width < 1 || width > VIEWPORT_LIMIT ||
		height < 1 || height > VIEWPORT_LIMIT ||
		!(aspect >= ASPECT_MIN && aspect <= ASPECT_MAX) ||
		!(focal >= FACTOR_MIN && focal <= FACTOR_MAX) ||
		!(near_distance >= FACTOR_MIN && near_distance <= FACTOR_MAX) ||
		!(far_distance > near_distance && far_distance <= VALUE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid projection setup");

	memset(&projection, 0, sizeof(projection));
	projection.clip.near_distance = near_distance;
	projection.clip.inverse_near = (float)(1 / (long double)near_distance);
	projection.clip.focal = focal;
	projection.clip.half_width = width * .5f;
	projection.clip.center[0] = width * .5f;
	projection.clip.center[1] = height * .5f;
	projection.clip.extent[0] = (float)width;
	projection.clip.extent[1] = (float)height;
	ratio = (long double)ORIGINAL_ASPECT / aspect;
	projection.clip.aspect = (float)ratio;
	stored_ratio = (double)ratio;
	projection.inverse_aspect = (float)(1 / (long double)stored_ratio);
	projection.far_distance = far_distance;
	projection.inverse_far = (float)(1 / (long double)far_distance);
	projection.inverse_focal = (float)(1 / (long double)focal);
	projection.inverse_half_width =
		(float)(1 / (long double)projection.clip.half_width);
	projection.scale[0] =
		(float)((long double)focal * projection.clip.half_width);
	projection.scale[1] = (float)(-(long double)focal *
		projection.clip.half_width * projection.clip.aspect);

	if (!valid_projection(&projection))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"projection setup exceeds native range");

	*output = projection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * The original tests signed binary32 words, so negative zero is outside.
 * Compare representations only after establishing a numerical zero.
 */
static int
negative_word(float value)
{
	const float zero = 0;

	return value < 0 ||
		(value == 0 && memcmp(&value, &zero, sizeof(value)) != 0);
}

static long double
transform_axis(const float matrix[12], const float point[3], unsigned int axis)
{
	return ((long double)point[0] * matrix[axis] +
		(long double)point[1] * matrix[axis + 3]) +
		((long double)point[2] * matrix[axis + 6] + matrix[axis + 9]);
}

enum mh_error_code
mh_project_point(const struct mh_projection *projection,
	const float transform[12], const float point[3],
	struct mh_projected_point *output, struct mh_error *error_out)
{
	long double x, y, z, reciprocal;
	float values[4], difference;
	unsigned int component, value_count, outcode;

	if (!valid_projection(projection) || transform == NULL ||
		point == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid point projection");

	for (component = 0; component < 12; component++)
		if (!bounded(transform[component]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid projection transform");

	for (component = 0; component < 3; component++)
		if (!bounded(point[component]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid projection point");

	z = transform_axis(transform, point, 2);
	difference = (float)((long double)projection->far_distance - z);

	if (difference <= 0) {
		output->outcode = MH_CLIP_FAR;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	x = transform_axis(transform, point, 0);
	y = transform_axis(transform, point, 1);
	difference = (float)(z - projection->clip.near_distance);

	if (difference <= 0) {
		values[0] = (float)x;
		values[1] = (float)y;
		values[2] = (float)z;
		value_count = 3;
		outcode = MH_CLIP_NEAR;
	} else {
		reciprocal = 1 / z;
		values[0] = (float)(x * projection->scale[0] * reciprocal +
			projection->clip.center[0]);
		values[1] = (float)(y * projection->scale[1] * reciprocal +
			projection->clip.center[1]);
		values[2] = (float)(z * projection->inverse_far);
		values[3] = (float)reciprocal;
		value_count = 4;
		outcode = 0;

		if (negative_word(values[0]))
			outcode |= MH_CLIP_LEFT;

		if (negative_word(values[1]))
			outcode |= MH_CLIP_TOP;

		if (negative_word(
			(float)((long double)projection->clip.extent[0] -
				values[0])))
			outcode |= MH_CLIP_RIGHT;

		if (negative_word(
			(float)((long double)projection->clip.extent[1] -
				values[1])))
			outcode |= MH_CLIP_BOTTOM;
	}

	for (component = 0; component < value_count; component++)
		if (!bounded(values[component]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"point projection exceeds native range");

	memcpy(output->value, values, value_count * sizeof(float));
	output->outcode = outcode;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_projection_restore(const struct mh_projection *projection,
	const struct mh_projected_point *point, float camera[3],
	struct mh_error *error_out)
{
	long double depth, factor, x, y;
	float values[3], dx, dy;
	unsigned int axis;

	if (!valid_projection(projection) || point == NULL || camera == NULL ||
		point->outcode > (MH_CLIP_VIEWPORT | MH_CLIP_NEAR))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid projection reconstruction");

	for (axis = 0; axis < 3; axis++)
		if (!bounded(point->value[axis]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid reconstruction point");

	if (point->outcode & MH_CLIP_NEAR)
		memcpy(values, point->value, sizeof(values));
	else {
		if (!(point->value[2] > 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid reconstruction depth");

		depth = (long double)point->value[2] * projection->far_distance;
		values[2] = (float)depth;
		factor = depth * projection->inverse_focal *
			projection->inverse_half_width;
		dx = (float)((long double)point->value[0] -
			projection->clip.center[0]);
		dy = (float)((long double)projection->clip.center[1] -
			point->value[1]);
		x = (long double)dx * factor;
		y = factor * projection->inverse_aspect * dy;

		if (!(x >= -FLT_MAX && x <= FLT_MAX && y >= -FLT_MAX &&
			y <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"reconstruction exceeds native range");

		values[0] = (float)x;
		values[1] = (float)y;
	}

	for (axis = 0; axis < 3; axis++)
		if (!bounded(values[axis]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"reconstruction exceeds native range");

	memcpy(camera, values, sizeof(values));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
