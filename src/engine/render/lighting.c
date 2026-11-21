#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/light_product.h"
#include "engine/render/lighting.h"

#define AMBIENT_SCALE 1.4
#define DIFFUSE_SCALE 1.5
#define SPECULAR_CUTOFF 0.5
#define MAX_ATTENUATION 0.4
#define NEGATIVE_LIGHT_SCALE 0.25
#define UNAVAILABLE_DEPTH_SCALE 0.4
#define DIRECTIONAL_COLOR_SCALE 0.0039215689f /* 0x3b808081. */

enum {
	UNAVAILABLE_MIN = 5,
	UNAVAILABLE_MAX = 70,
	CAR_HIGHLIGHT_SCALE = 4
};

enum mh_error_code
mh_light_directional_color(const unsigned char rgb[3], float intensity,
	float color[3], struct mh_error *error_out)
{
	float result[3], product;
	double value;
	unsigned int channel;

	if (rgb == NULL || color == NULL ||
		!(intensity >= -FLT_MAX && intensity <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid directional light color");

	for (channel = 0; channel < 3; channel++) {
		value = (double)rgb[channel] * intensity;

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"directional light color overflow");

		product = (float)value;
		result[channel] =
			(float)((double)product * DIRECTIONAL_COLOR_SCALE);
	}

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
bounded_vector(const double vector[3])
{
	unsigned int axis;

	if (vector == NULL)
		return 0;

	for (axis = 0; axis < 3; axis++) {
		if (!(vector[axis] >= -FLT_MAX && vector[axis] <= FLT_MAX))
			return 0;
	}

	return 1;
}

static double
normalize_vector(double vector[3])
{
	double length;
	unsigned int axis;

	length = sqrt(vector[0] * vector[0] + vector[1] * vector[1] +
		vector[2] * vector[2]);

	if (length != 0) {
		for (axis = 0; axis < 3; axis++)
			vector[axis] /= length;
	}

	return length;
}

enum mh_error_code
mh_light_unavailable(const unsigned char rgb[3], float depth,
	const float light[3], float color[3], struct mh_error *error_out)
{
	float result[3];
	volatile float scaled;
	long double value;
	unsigned int channel;

	if (rgb == NULL || light == NULL || color == NULL ||
		!(depth >= -FLT_MAX && depth <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid unavailable material");

	for (channel = 0; channel < 3; channel++) {
		if (!(light[channel] >= -FLT_MAX && light[channel] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid unavailable lighting");

		/*
		 * Load a binary64 .4, then keep extended precision until its
		 * binary32 store. This matters at exact halfway cases.
		 */
		value = (1.0L - depth) * (long double)UNAVAILABLE_DEPTH_SCALE *
			rgb[channel];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"unavailable material overflow");

		scaled = (float)value;
		value = (double)scaled * light[channel];

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"unavailable lighting overflow");

		result[channel] = (float)value;

		if (result[channel] < UNAVAILABLE_MIN)
			result[channel] = UNAVAILABLE_MIN;

		if (result[channel] > UNAVAILABLE_MAX)
			result[channel] = UNAVAILABLE_MAX;
	}

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_light_prepare(const struct mh_point_light *light, const double origin[3],
	const double eye[3], double intensity,
	struct mh_prepared_light *prepared, struct mh_error *error_out)
{
	struct mh_prepared_light result;
	double distance;
	double attenuation;
	unsigned int axis, channel;

	if (light == NULL || prepared == NULL || !bounded_vector(origin) ||
		!bounded_vector(eye) || !bounded_vector(light->position) ||
		!bounded_vector(light->color) ||
		!(light->intensity >= -FLT_MAX &&
			light->intensity <= FLT_MAX) ||
		!(light->radius >= -FLT_MAX && light->radius <= FLT_MAX) ||
		!(intensity >= 0 && intensity <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid point light input");

	memset(&result, 0, sizeof(result));

	for (axis = 0; axis < 3; axis++) {
		result.direction[axis] = light->position[axis] - origin[axis];
		result.view[axis] = eye[axis] - origin[axis];
	}

	distance = normalize_vector(result.direction);
	normalize_vector(result.view);
	attenuation = 0.0;

	if (light->radius > 1.0 && distance <= light->radius) {
		attenuation = 1.0 - distance / light->radius;
	}

	attenuation = attenuation * attenuation * intensity * light->intensity;

	if (attenuation > MAX_ATTENUATION)
		attenuation = MAX_ATTENUATION;

	if (attenuation < 0)
		attenuation *= NEGATIVE_LIGHT_SCALE;

	for (channel = 0; channel < 3; channel++) {
		result.color[channel] = light->color[channel] * attenuation;
	}

	if (!bounded_vector(result.color))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"point light exceeds float range");

	result.specular = light->intensity > 0;
	*prepared = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_light_shade(const struct mh_prepared_light *lights, size_t light_count,
	const double normal[3], const double ambient[3], double color[3],
	struct mh_error *error_out)
{
	const struct mh_prepared_light *light;
	double result[3];
	double diffuse;
	double specular;
	size_t light_index;
	unsigned int axis, channel;

	if ((light_count != 0 && lights == NULL) || color == NULL ||
		!bounded_vector(normal) || !bounded_vector(ambient))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lighting input");

	for (channel = 0; channel < 3; channel++)
		result[channel] = AMBIENT_SCALE * ambient[channel];

	for (light_index = 0; light_index < light_count; light_index++) {
		light = &lights[light_index];

		if (!bounded_vector(light->direction) ||
			!bounded_vector(light->view) ||
			!bounded_vector(light->color))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid prepared light");

		diffuse = 0;

		for (axis = 0; axis < 3; axis++)
			diffuse += normal[axis] * light->direction[axis];

		if (diffuse <= 0)
			continue;

		specular = 0;

		if (light->specular) {
			for (axis = 0; axis < 3; axis++) {
				specular += (2.0 * diffuse * normal[axis] -
						light->direction[axis]) *
					light->view[axis];
			}

			if (specular > SPECULAR_CUTOFF) {
				specular *= specular;
				specular *= specular;
			} else
				specular = 0;
		}

		for (channel = 0; channel < 3; channel++)
			result[channel] +=
				(DIFFUSE_SCALE * diffuse + specular) *
				light->color[channel];
	}

	if (!bounded_vector(result))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"lighting exceeds float range");

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static long double
normal_dot(const float a[3], const float b[3])
{
	mh_light_product x, y, z;

	x = (mh_light_product)a[0] * b[0];
	y = (mh_light_product)a[1] * b[1];
	z = (mh_light_product)a[2] * b[2];

	/* Preserve the extended sums and their original order. */

	return ((long double)x + y) + z;
}

/* Explicit stores for lighting modes 2 and 4. */
enum mh_error_code
mh_light_shade_mode(const struct mh_prepared_light *lights, size_t light_count,
	const double normal[3], const double ambient[3], unsigned int mode,
	float color[3], struct mh_error *error_out)
{
	const struct mh_prepared_light *light;
	float result[3], stored_normal[3], direction[3], reflection[3], view[3],
		diffuse, highlight, twice_diffuse, square;
	long double dot, alignment, value;
	size_t light_index;
	unsigned int axis, channel;

	if ((mode != MH_LIGHT_OBJECT && mode != MH_LIGHT_CAR) ||
		color == NULL || (light_count && lights == NULL) ||
		!bounded_vector(normal) || !bounded_vector(ambient))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid normal lighting mode");

	for (axis = 0; axis < 3; axis++) {
		stored_normal[axis] = (float)normal[axis];
		value = (long double)(float)ambient[axis] *
			(double)AMBIENT_SCALE;

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"normal lighting overflow");

		result[axis] = (float)value;
	}

	for (light_index = 0; light_index < light_count; light_index++) {
		light = &lights[light_index];

		if (!bounded_vector(light->direction) ||
			!bounded_vector(light->view) ||
			!bounded_vector(light->color))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid prepared normal light");

		for (axis = 0; axis < 3; axis++)
			direction[axis] = (float)light->direction[axis];

		dot = normal_dot(stored_normal, direction);

		if (!(dot > 0))
			continue;

		diffuse = (float)dot;
		highlight = 0;

		if (light->specular) {
			twice_diffuse = (float)((mh_light_product)diffuse * 2);

			for (axis = 0; axis < 3; axis++) {
				reflection[axis] =
					(float)((mh_light_product)
							stored_normal[axis] *
						twice_diffuse);
				reflection[axis] =
					(float)((long double)reflection[axis] -
						direction[axis]);
				view[axis] = (float)light->view[axis];
			}

			alignment = normal_dot(reflection, view);

			if (alignment > SPECULAR_CUTOFF) {
				highlight = (float)alignment;
				value = (mh_light_product)highlight * highlight;
				square = (float)value;
				highlight = (float)(value * square *
					(mode == MH_LIGHT_CAR
							? CAR_HIGHLIGHT_SCALE
							: 1));
			}
		}

		diffuse = (float)((mh_light_product)diffuse * DIFFUSE_SCALE);

		for (channel = 0; channel < 3; channel++) {
			value = ((long double)highlight + diffuse) *
					(float)light->color[channel] +
				result[channel];

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"normal lighting overflow");

			result[channel] = (float)value;
		}
	}

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
