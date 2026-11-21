#include <float.h>
#include <math.h>
#include <string.h>

#include "backends/render/software_skid_light.h"

#define AMBIENT_SCALE 2.5
#define POINT_SCALE 1.75
#define NEGATIVE_SCALE .25
#define MINIMUM_RADIUS .1

static int
store_float(float *output, long double value)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

static int
point_distance(const float position[3], const float sample[3], float *output)
{
	float delta, square[3], sum;
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		if (!store_float(&delta,
			(long double)position[axis] - sample[axis]) ||
			!store_float(&square[axis], (long double)delta * delta))
			return 0;

	if (!store_float(&sum, (long double)square[1] + square[0]) ||
		!store_float(&sum, (long double)sum + square[2]))
		return 0;

	*output = (float)sqrt((double)sum);

	return 1;
}

static enum mh_error_code
add_point_light(const struct mh_point_light *light, const float sample[3],
	float shade[3], struct mh_error *error_out)
{
	float position[3], rgb[3], radius, intensity, length, attenuation;
	float amount, product;
	unsigned int i;

	if (!store_float(&radius, light->radius) ||
		!store_float(&intensity, light->intensity))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software skid light");

	for (i = 0; i < 3; i++)
		if (!store_float(&position[i], light->position[i]) ||
			!store_float(&rgb[i], light->color[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software skid light vector");

	if (!point_distance(position, sample, &length))
		goto overflow;

	attenuation = 0;

	if (radius > MINIMUM_RADIUS) {
		if (!store_float(&attenuation, (long double)length / radius) ||
			!store_float(&attenuation, 1.0L - attenuation))
			goto overflow;

		if (attenuation <= 0)
			attenuation = 0;
	}

	if (!store_float(&amount, (long double)attenuation * attenuation) ||
		!store_float(&amount, (long double)amount * POINT_SCALE) ||
		!store_float(&amount, (long double)amount * intensity))
		goto overflow;

	if (amount < 0)
		amount = (float)((long double)amount * NEGATIVE_SCALE);

	for (i = 0; i < 3; i++)
		if (!store_float(&product, (long double)rgb[i] * amount) ||
			!store_float(
				&shade[i], (long double)shade[i] + product))
			goto overflow;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software skid point lighting overflow");
}

enum mh_error_code
mh_software_skid_color(const struct mh_model_lighting *lighting,
	const float sample[3], const float material[3], float color[3],
	struct mh_error *error_out)
{
	float sum, shade[3], value;
	unsigned int channel;
	size_t light;
	enum mh_error_code status;

	if (material == NULL || color == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software skid color output");

	for (channel = 0; channel < 3; channel++)
		if (!(material[channel] >= -FLT_MAX &&
			material[channel] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software skid material");

	if (!store_float(&sum, (long double)material[0] + material[1]) ||
		!store_float(&sum, (long double)sum + material[2]))
		goto overflow;

	if (sum <= 1) {
		memmove(color, material, 3 * sizeof(*color));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (lighting == NULL || sample == NULL ||
		(lighting->light_count != 0 && lighting->lights == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software skid lighting");

	for (channel = 0; channel < 3; channel++) {
		if (!(sample[channel] >= -FLT_MAX &&
			sample[channel] <= FLT_MAX) ||
			!(lighting->ambient[channel] >= -FLT_MAX &&
				lighting->ambient[channel] <= FLT_MAX) ||
			!(lighting->brightness[channel] >= -FLT_MAX &&
				lighting->brightness[channel] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software skid lighting vector");

		if (!store_float(&shade[channel],
			(long double)lighting->ambient[channel] *
				AMBIENT_SCALE))
			goto overflow;
	}

	for (light = 0; light < lighting->light_count; light++) {
		status = add_point_light(
			lighting->lights + light, sample, shade, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (channel = 0; channel < 3; channel++)
		if (!store_float(&value,
			(long double)shade[channel] * material[channel]) ||
			!store_float(&shade[channel],
				(long double)value *
					lighting->brightness[channel]))
			goto overflow;

	memcpy(color, shade, sizeof(shade));
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software skid material lighting overflow");
}
