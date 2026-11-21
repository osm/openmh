#include <float.h>
#include <string.h>

#include "backends/render/software_reflection.h"

/* Round the scaled texture dimensions to float after multiplying. */
#define NORMAL_SCALE .495
#define TEXEL_LIMIT 255
#define IMAGE_LIMIT 256

static int
store_float(float *output, long double value)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

/* Only the rotated normal's X/Y are consumed by the producers. */
static int
normal_xy(const float camera[12], const float normal[3], float output[2])
{
	float product[3], sum;
	unsigned int i, k;

	for (i = 0; i < 2; i++) {
		for (k = 0; k < 3; k++)
			if (!store_float(&product[k],
				(long double)normal[k] * camera[k * 3 + i]))
				return 0;

		if (!store_float(&sum, (long double)product[1] + product[0]) ||
			!store_float(&output[i], (long double)sum + product[2]))
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_software_reflection_uv(const struct mh_projection *projection,
	const float camera[12], const float normal[3],
	const struct mh_software_point *point, unsigned int width,
	unsigned int height, float uv[2], struct mh_error *error_out)
{
	float half[2], scale[2], screen_scale, inverse_center;
	float rotated[2], offset[2], product, result[2];
	unsigned int i;

	if (mh_software_projection_check(projection, NULL) != MH_ERROR_NONE ||
		camera == NULL || normal == NULL || point == NULL ||
		uv == NULL || width == 0 || height == 0 ||
		width > IMAGE_LIMIT || height > IMAGE_LIMIT ||
		point->outcode > (MH_CLIP_NEAR | MH_CLIP_VIEWPORT) ||
		!(projection->clip.center[0] > 0) ||
		!(projection->clip.inverse_near > 0 &&
			projection->clip.inverse_near <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software reflection input");

	for (i = 0; i < 9; i++)
		if (!(camera[i] >= -FLT_MAX && camera[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software reflection rotation");

	for (i = 0; i < 3; i++)
		if (!(normal[i] >= -FLT_MAX && normal[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software reflection normal");

	for (i = 0; i < 2; i++)
		if (!(point->value[i] >= -FLT_MAX &&
			point->value[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software reflection point");

	half[0] = width * .5f;
	half[1] = height * .5f;

	if (!store_float(&inverse_center, 1.0L / projection->clip.center[0]) ||
		!normal_xy(camera, normal, rotated))
		goto overflow;

	for (i = 0; i < 2; i++)
		scale[i] = (float)((long double)half[i] * NORMAL_SCALE);

	if (!store_float(&screen_scale, (long double)scale[0] * inverse_center))
		goto overflow;

	if (point->outcode & MH_CLIP_NEAR) {
		for (i = 0; i < 2; i++)
			if (!store_float(&offset[i],
				(long double)point->value[i] *
					projection->clip.inverse_near) ||
				!store_float(&offset[i],
					(long double)offset[i] *
						projection->clip.center[0]) ||
				!store_float(&offset[i],
					(long double)offset[i] * screen_scale))
				goto overflow;
	} else {
		if (!store_float(&offset[0],
			(long double)point->value[0] -
				projection->clip.center[0]) ||
			!store_float(&offset[1],
				(long double)projection->clip.center[1] -
					point->value[1]))
			goto overflow;

		for (i = 0; i < 2; i++)
			if (!store_float(&offset[i],
				(long double)offset[i] * screen_scale))
				goto overflow;
	}

	for (i = 0; i < 2; i++) {
		if (!store_float(
			&product, (long double)scale[i] * rotated[i]) ||
			!store_float(
				&result[i], (long double)half[i] + offset[i]) ||
			!store_float(
				&result[i], (long double)result[i] + product))
			goto overflow;

		result[i] = result[i] < 0	  ? 0
			: result[i] > TEXEL_LIMIT ? TEXEL_LIMIT
						: result[i];
	}

	memcpy(uv, result, sizeof(result));
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software reflection coordinate overflow");
}
