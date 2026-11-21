#include <float.h>
#include <math.h>
#include <string.h>

#include "backends/render/software_lighting.h"

#define ATTENUATION_CAP .4
#define NEGATIVE_SCALE .25f

static int
store_float(float *output, long double value)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

static int
local_point(const float transform[12], const float point[3], float output[3])
{
	float delta[3], product[3], sum;
	unsigned int i, k;

	for (i = 0; i < 3; i++)
		if (!store_float(&delta[i],
			(long double)point[i] - transform[9 + i]))
			return 0;

	for (i = 0; i < 3; i++) {
		for (k = 0; k < 3; k++)
			if (!store_float(&product[k],
				(long double)delta[k] *
					transform[i * 3 + k]))
				return 0;

		if (!store_float(&sum, (long double)product[1] + product[0]) ||
			!store_float(&output[i], (long double)sum + product[2]))
			return 0;
	}

	return 1;
}

static int
vector_length(const float vector[3], float *output)
{
	float square[3], sum;
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!store_float(
			&square[i], (long double)vector[i] * vector[i]))
			return 0;

	if (!store_float(&sum, (long double)square[1] + square[0]) ||
		!store_float(&sum, (long double)sum + square[2]))
		return 0;

	*output = (float)sqrt((double)sum);

	return 1;
}

static int
normalize(const float vector[3], float distance, double output[3])
{
	float inverse, value;
	unsigned int i;

	if (distance == 0) {
		for (i = 0; i < 3; i++)
			output[i] = 0;

		return 1;
	}

	if (!store_float(&inverse, 1.0L / distance))
		return 0;

	for (i = 0; i < 3; i++) {
		if (!store_float(&value, (long double)vector[i] * inverse))
			return 0;

		output[i] = value;
	}

	return 1;
}

enum mh_error_code
mh_software_light_prepare(const struct mh_point_light *light,
	const float transform[12], const float eye[3], float intensity,
	struct mh_prepared_light *prepared, struct mh_error *error_out)
{
	struct mh_prepared_light result;
	float position[3], rgb[3], direction[3], view[3], radius,
		source_intensity;
	float distance, eye_distance, attenuation, amount, value;
	unsigned int i;

	if (light == NULL || transform == NULL || eye == NULL ||
		prepared == NULL || !(intensity >= 0 && intensity <= FLT_MAX) ||
		!store_float(&radius, light->radius) ||
		!store_float(&source_intensity, light->intensity))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software point light");

	for (i = 0; i < 12; i++)
		if (!(transform[i] >= -FLT_MAX && transform[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software light transform");

	for (i = 0; i < 3; i++)
		if (!store_float(&position[i], light->position[i]) ||
			!store_float(&rgb[i], light->color[i]) ||
			!(eye[i] >= -FLT_MAX && eye[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software light vector");

	memset(&result, 0, sizeof(result));

	if (!local_point(transform, position, direction) ||
		!local_point(transform, eye, view) ||
		!vector_length(direction, &distance) ||
		!vector_length(view, &eye_distance) ||
		!normalize(direction, distance, result.direction) ||
		!normalize(view, eye_distance, result.view))
		goto overflow;

	attenuation = 0;

	if (radius > 1 && distance <= radius) {
		if (!store_float(&value, (long double)distance / radius) ||
			!store_float(&attenuation, 1.0L - value))
			goto overflow;
	}

	if (!store_float(&amount, (long double)attenuation * attenuation) ||
		!store_float(&amount, (long double)amount * intensity) ||
		!store_float(&amount, (long double)amount * source_intensity))
		goto overflow;

	if (amount > ATTENUATION_CAP)
		amount = (float)ATTENUATION_CAP;

	if (amount < 0)
		amount = (float)((long double)amount * NEGATIVE_SCALE);

	for (i = 0; i < 3; i++) {
		if (!store_float(&value, (long double)rgb[i] * amount))
			goto overflow;

		result.color[i] = value;
	}

	result.specular = source_intensity > 0;
	*prepared = result;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software light preparation overflow");
}
