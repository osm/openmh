#include <stddef.h>

#include "engine/render/glenz.h"
#include "engine/render/powervr_environment.h"

#define COORDINATE_LIMIT 1000000
#define NORMAL_SCALE 0.24950000643730164f
#define SCREEN_SCALE 0.2495

enum mh_error_code
mh_powervr_environment_model_uv(const struct mh_projection *projection,
	const struct mh_projected_point *point, const float rotation[9],
	const float normal[3], float uv[2], struct mh_error *error_out)
{
	float camera_normal[3];
	unsigned int i;

	if (rotation == NULL || normal == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid environment rotation");

	for (i = 0; i < 9; i++)
		if (!(rotation[i] >= -COORDINATE_LIMIT &&
			rotation[i] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid environment rotation");

	for (i = 0; i < 3; i++)
		if (!(normal[i] >= -COORDINATE_LIMIT &&
			normal[i] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid environment normal");

	/* Accumulate Y + X + Z, then store each binary32 component. */
	for (i = 0; i < 3; i++)
		camera_normal[i] =
			(float)(((long double)rotation[3 + i] * normal[1] +
					(long double)rotation[i] * normal[0]) +
				(long double)rotation[6 + i] * normal[2]);

	return mh_powervr_environment_uv(
		projection, point, camera_normal, uv, error_out);
}

enum mh_error_code
mh_powervr_environment_uv(const struct mh_projection *projection,
	const struct mh_projected_point *point, const float normal[3],
	float uv[2], struct mh_error *error_out)
{
	float inverse_near, scale, offset, output[2];
	unsigned int i;

	if (projection == NULL || point == NULL || normal == NULL ||
		uv == NULL || point->outcode > MH_CLIP_ALL ||
		!(projection->clip.near_distance > 0 &&
			projection->clip.near_distance <= COORDINATE_LIMIT) ||
		!(projection->clip.center[0] > 0 &&
			projection->clip.center[0] <= COORDINATE_LIMIT) ||
		!(projection->inverse_half_width > 0 &&
			projection->inverse_half_width <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid environment projection");

	for (i = 0; i < 3; i++)
		if (!(normal[i] >= -COORDINATE_LIMIT &&
			normal[i] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid environment normal");

	if (!(point->outcode & MH_CLIP_NEAR))
		return mh_glenz_phong_uv(normal, point->value,
			projection->clip.center, projection->inverse_half_width,
			uv, error_out);

	inverse_near = (float)(1.0L / projection->clip.near_distance);
	scale = (float)((long double)projection->inverse_half_width *
		SCREEN_SCALE);

	for (i = 0; i < 2; i++) {
		if (!(point->value[i] >= -COORDINATE_LIMIT &&
			point->value[i] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid environment point");

		/* Round the offset once, after all three multiplications. */
		offset = (float)(((long double)point->value[i] * inverse_near *
					projection->clip.center[0]) *
			scale);
		output[i] = (float)(((long double)offset + .5L) +
			(long double)normal[i] * NORMAL_SCALE);

		if (output[i] < 0)
			output[i] = 0;
		else if (output[i] > 1)
			output[i] = 1;
	}

	uv[0] = output[0];
	uv[1] = output[1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
