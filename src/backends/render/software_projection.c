#include <string.h>

#include "backends/render/software_projection.h"
#include "engine/core/limits.h"

#define VALUE_LIMIT 1e9f
#define FACTOR_MIN 1e-6f
#define FACTOR_MAX 1e6f
#define VIEWPORT_LIMIT 16384

/*
 * Native affine view/model composition; original projection starts afterward.
 */
int
mh_software_camera_transform(
	const float view[16], const float *model, float camera[12])
{
	unsigned int i, row, col, k;
	double value;

	if (view == NULL)
		return 0;

	for (i = 0; i < 16; i++)
		if (!(view[i] >= -VALUE_LIMIT && view[i] <= VALUE_LIMIT) ||
			(model != NULL &&
				!(model[i] >= -VALUE_LIMIT &&
					model[i] <= VALUE_LIMIT)))
			return 0;

	if (view[3] != 0 || view[7] != 0 || view[11] != 0 || view[15] != 1 ||
		(model != NULL &&
			(model[3] != 0 || model[7] != 0 || model[11] != 0 ||
				model[15] != 1)))
		return 0;

	for (col = 0; col < 4; col++) {
		for (row = 0; row < 3; row++) {
			value = view[col * 4 + row];

			if (model != NULL) {
				value = 0;

				for (k = 0; k < 4; k++)
					value += (double)view[k * 4 + row] *
						model[col * 4 + k];
			}

			if (!(value >= -VALUE_LIMIT && value <= VALUE_LIMIT))
				return 0;

			camera[col * 3 + row] =
				(float)(row == 2 ? -value : value);
		}
	}

	return 1;
}

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

enum mh_error_code
mh_software_projection_check(
	const struct mh_projection *projection, struct mh_error *error_out)
{
	unsigned int i;

	if (projection == NULL ||
		!(projection->clip.near_distance >= FACTOR_MIN &&
			projection->clip.near_distance <= FACTOR_MAX) ||
		!(projection->far_distance > projection->clip.near_distance &&
			projection->far_distance <= VALUE_LIMIT) ||
		!(projection->clip.focal > 0 &&
			projection->clip.focal <= FACTOR_MAX) ||
		!(projection->clip.half_width > 0 &&
			projection->clip.half_width <= VIEWPORT_LIMIT) ||
		!(projection->clip.aspect > 0 &&
			projection->clip.aspect <= FACTOR_MAX) ||
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
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software projection lens");

	for (i = 0; i < 2; i++)
		if (!bounded(projection->clip.center[i]) ||
			!(projection->clip.extent[i] >= 1 &&
				projection->clip.extent[i] <= VIEWPORT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software projection viewport");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
negative_word(float value)
{
	const float zero = 0;

	return value < 0 ||
		(value == 0 && memcmp(&value, &zero, sizeof(value)) != 0);
}

/* Round each product and partial sum to float for the software projection. */
static float
transform_axis(
	const float transform[12], const float point[3], unsigned int axis)
{
	float a, b, c;

	a = (float)((double)point[0] * transform[axis]);
	b = (float)((double)point[1] * transform[axis + 3]);
	c = (float)((double)point[2] * transform[axis + 6]);
	a = (float)((double)a + b);
	c = (float)((double)c + transform[axis + 9]);

	return (float)((double)a + c);
}

enum mh_error_code
mh_software_project_point(const struct mh_projection *projection,
	const float transform[12], const float point[3],
	struct mh_software_point *output, struct mh_error *error_out)
{
	float x, y, z, reciprocal, values[4], difference;
	unsigned int i, value_count, outcode;
	enum mh_error_code status;

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (transform == NULL || point == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software projection inputs");

	for (i = 0; i < 12; i++)
		if (!bounded(transform[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software camera transform");

	for (i = 0; i < 3; i++)
		if (!bounded(point[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software projection point");

	z = transform_axis(transform, point, 2);
	difference = (float)((double)projection->far_distance - z);

	if (difference <= 0) {
		output->outcode = MH_CLIP_FAR;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	x = transform_axis(transform, point, 0);
	y = transform_axis(transform, point, 1);
	difference = (float)((double)z - projection->clip.near_distance);

	if (difference <= 0) {
		values[0] = x;
		values[1] = y;
		values[2] = z;
		value_count = 3;
		outcode = MH_CLIP_NEAR;
	} else {
		reciprocal = (float)(1.0 / z);
		x = (float)((double)x * projection->scale[0]);
		y = (float)((double)y * projection->scale[1]);
		x = (float)((double)x * reciprocal);
		y = (float)((double)y * reciprocal);
		values[0] = (float)((double)x + projection->clip.center[0]);
		values[1] = (float)((double)y + projection->clip.center[1]);
		values[2] = z;
		values[3] = reciprocal;
		value_count = 4;
		outcode = 0;

		if (negative_word(values[0]))
			outcode |= MH_CLIP_LEFT;

		if (negative_word(values[1]))
			outcode |= MH_CLIP_TOP;

		if (negative_word((float)((double)projection->clip.extent[0] -
			values[0])))
			outcode |= MH_CLIP_RIGHT;

		if (negative_word((float)((double)projection->clip.extent[1] -
			values[1])))
			outcode |= MH_CLIP_BOTTOM;
	}

	for (i = 0; i < value_count; i++)
		if (!bounded(values[i]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software projected point exceeds native range");

	memcpy(output->value, values, value_count * sizeof(float));
	output->outcode = outcode;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_projection_restore(const struct mh_projection *projection,
	const struct mh_software_point *point, float camera[3],
	struct mh_error *error_out)
{
	float values[3], factor, dx, dy;
	unsigned int i;
	enum mh_error_code status;

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (point == NULL || camera == NULL ||
		point->outcode > (MH_CLIP_NEAR | MH_CLIP_VIEWPORT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software reconstruction inputs");

	for (i = 0; i < 3; i++)
		if (!bounded(point->value[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software reconstruction point");

	memcpy(values, point->value, sizeof(values));

	if (!(point->outcode & MH_CLIP_NEAR)) {
		if (!(values[2] > 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software reconstruction depth");

		factor = (float)((double)values[2] * projection->inverse_focal);
		factor = (float)((double)factor *
			projection->inverse_half_width);
		dx = (float)((double)values[0] - projection->clip.center[0]);
		dy = (float)((double)projection->clip.center[1] - values[1]);
		values[0] = (float)((double)dx * factor);
		factor = (float)((double)factor * projection->inverse_aspect);
		values[1] = (float)((double)factor * dy);
	}

	for (i = 0; i < 3; i++)
		if (!bounded(values[i]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software reconstruction exceeds native range");

	memcpy(camera, values, sizeof(values));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_project_face(const struct mh_projection *projection,
	const float transform[12], unsigned int flags,
	const struct mh_near_corner corners[4],
	struct mh_software_clipped_face output[2], unsigned int *count,
	struct mh_software_point *points_out, struct mh_error *error_out)
{
	struct mh_software_point points[4];
	struct mh_software_clipped_face face;
	struct mh_near_corner restored[4];
	unsigned int i, corner_count, near_mask, common_code, combined_code;
	enum mh_error_code status;

	if (corners == NULL || output == NULL || count == NULL ||
		flags > MH_U16_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software face projection inputs");

	memset(points, 0, sizeof(points));
	memset(&face, 0, sizeof(face));
	memset(restored, 0, sizeof(restored));
	corner_count = flags & MH_NEAR_QUAD ? 4 : 3;
	near_mask = combined_code = 0;
	common_code = MH_CLIP_VIEWPORT | MH_CLIP_NEAR | MH_CLIP_FAR;

	for (i = 0; i < corner_count; i++) {
		status = mh_software_project_point(projection, transform,
			corners[i].camera, &points[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		common_code &= points[i].outcode;
		combined_code |= points[i].outcode;

		if (points[i].outcode & MH_CLIP_NEAR)
			near_mask |= 1U << i;
	}

	/* Any far point aborts; there is no far-plane splitting. */
	if ((combined_code & MH_CLIP_FAR) || common_code != 0) {
		*count = 0;

		if (points_out != NULL)
			memcpy(points_out, points, sizeof(points));

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (near_mask != 0) {
		for (i = 0; i < corner_count; i++) {
			restored[i] = corners[i];
			status = mh_software_projection_restore(projection,
				&points[i], restored[i].camera, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_software_clip_near(&projection->clip, flags,
			near_mask, restored, output, count, error_out);

		if (status == MH_ERROR_NONE && points_out != NULL)
			memcpy(points_out, points, sizeof(points));

		return status;
	}

	face.flags = flags;
	face.count = corner_count;

	for (i = 0; i < corner_count; i++) {
		memcpy(face.corners[i].xy, points[i].value,
			sizeof(face.corners[i].xy));
		face.corners[i].weight = points[i].value[3];
		memcpy(face.corners[i].uv, corners[i].uv,
			sizeof(face.corners[i].uv));
		memcpy(face.corners[i].rgb, corners[i].rgb,
			sizeof(face.corners[i].rgb));
	}

	output[0] = face;
	*count = 1;

	if (points_out != NULL)
		memcpy(points_out, points, sizeof(points));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
