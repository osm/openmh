#include <float.h>
#include <string.h>

#include "backends/render/software_lighting.h"
#include "backends/render/software_material.h"

#define SPECULAR_CUTOFF .5f
#define UNIFORM_SCALE 1.5f
#define CAR_BASE .8f
#define DETAIL_DIM .75f

enum {
	TEXTURED_BASE = 128
};

static int
store_float(float *output, long double value)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

static int
finite_vector(const float vector[3])
{
	unsigned int i;

	if (vector == NULL)
		return 0;

	for (i = 0; i < 3; i++)
		if (!(vector[i] >= -FLT_MAX && vector[i] <= FLT_MAX))
			return 0;

	return 1;
}

/* Three products, then (y + x) + z. */
static int
dot(const float a[3], const float b[3], float *value)
{
	float product[3], sum;
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!store_float(&product[i], (long double)a[i] * b[i]))
			return 0;

	return store_float(&sum, (long double)product[1] + product[0]) &&
		store_float(value, (long double)sum + product[2]);
}

static int
accumulate(float result[3], const float color[3], float strength)
{
	float product;
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!store_float(&product, (long double)strength * color[i]) ||
			!store_float(
				&result[i], (long double)result[i] + product))
			return 0;

	return 1;
}

static int
normal_strength(const float normal[3], const float direction[3],
	const float view[3], int specular, float scale, float *strength)
{
	float diffuse, twice, reflection[3], alignment, product, highlight;
	unsigned int i;

	if (!dot(normal, direction, &diffuse))
		return 0;

	*strength = 0;

	if (diffuse <= 0)
		return 1;

	*strength = diffuse;

	if (!specular)
		return 1;

	if (!store_float(&twice, (long double)diffuse * 2))
		return 0;

	for (i = 0; i < 3; i++)
		if (!store_float(&product, (long double)twice * normal[i]) ||
			!store_float(&reflection[i],
				(long double)product - direction[i]))
			return 0;

	if (!dot(reflection, view, &alignment))
		return 0;

	if (alignment <= SPECULAR_CUTOFF)
		return 1;

	return store_float(&product, (long double)alignment * alignment) &&
		store_float(&highlight, (long double)product * product) &&
		store_float(&highlight, (long double)highlight * scale) &&
		store_float(strength, (long double)highlight + diffuse);
}

static int
car_facing(float result[3], float normal_z)
{
	float factor, square;
	unsigned int i;

	factor = CAR_BASE;

	if (normal_z < 0 &&
		(!store_float(&square, (long double)normal_z * normal_z) ||
			!store_float(&factor, (long double)square + CAR_BASE)))
		return 0;

	/* Multiply each channel twice, with stores. */
	for (i = 0; i < 3; i++)
		if (!store_float(&result[i], (long double)result[i] * factor) ||
			!store_float(
				&result[i], (long double)result[i] * factor))
			return 0;

	return 1;
}

enum mh_error_code
mh_software_light_shade(const struct mh_software_lighting *lighting,
	const float normal[3], enum mh_software_shade mode, float color[3],
	struct mh_error *error_out)
{
	const struct mh_prepared_light *light;
	float result[3], direction[3], view[3], rgb[3], strength;
	size_t index;
	unsigned int i;

	if (lighting == NULL || color == NULL ||
		(mode != MH_SOFTWARE_SHADE_NORMAL &&
			mode != MH_SOFTWARE_SHADE_UNIFORM &&
			mode != MH_SOFTWARE_SHADE_CAR) ||
		(lighting->count != 0 && lighting->lights == NULL) ||
		!finite_vector(lighting->ambient) ||
		!finite_vector(lighting->direction) ||
		!finite_vector(lighting->directional_color) ||
		(mode != MH_SOFTWARE_SHADE_UNIFORM && !finite_vector(normal)) ||
		!(lighting->specular_scale >= -FLT_MAX &&
			lighting->specular_scale <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software lighting input");

	memcpy(result, lighting->ambient, sizeof(result));

	if (mode == MH_SOFTWARE_SHADE_NORMAL) {
		if (!dot(normal, lighting->direction, &strength))
			goto overflow;

		if (strength > 0 &&
			!accumulate(
				result, lighting->directional_color, strength))
			goto overflow;
	}

	for (index = 0; index < lighting->count; index++) {
		light = &lighting->lights[index];

		for (i = 0; i < 3; i++)
			if (!store_float(&direction[i], light->direction[i]) ||
				!store_float(&view[i], light->view[i]) ||
				!store_float(&rgb[i], light->color[i]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software prepared light");

		strength = UNIFORM_SCALE;

		if (mode == MH_SOFTWARE_SHADE_NORMAL &&
			!normal_strength(normal, direction, view,
				light->specular, lighting->specular_scale,
				&strength))
			goto overflow;

		if (!accumulate(result, rgb, strength))
			goto overflow;
	}

	if (mode == MH_SOFTWARE_SHADE_CAR && !car_facing(result, normal[2]))
		goto overflow;

	for (i = 0; i < 3; i++)
		color[i] = result[i] < 0 ? 0 : result[i] > 1 ? 1 : result[i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "software lighting overflow");
}

enum mh_error_code
mh_software_light_color(unsigned int type, unsigned int flags,
	const unsigned char rgb[3], const float brightness[3],
	const float shade[3], float color[3], struct mh_error *error_out)
{
	float result[3], product;
	unsigned int i;

	if (type > 3 || flags > MH_SOFTWARE_MATERIAL_FLAG_MASK || rgb == NULL ||
		brightness == NULL || color == NULL ||
		(!(flags & MH_SOFTWARE_MATERIAL_EMISSIVE) && shade == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software lit material");

	for (i = 0; i < 3; i++) {
		if (!(brightness[i] >= 0 &&
			brightness[i] <= MH_SOFTWARE_BRIGHTNESS_MAX) ||
			(!(flags & MH_SOFTWARE_MATERIAL_EMISSIVE) &&
				!(shade[i] >= 0 && shade[i] <= 1)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software material shade");

		if (flags & MH_SOFTWARE_MATERIAL_EMISSIVE)
			result[i] =
				(float)((long double)rgb[i] * brightness[i]);
		else if (type < 2) {
			/* Shade * brightness, then stored RGB. */
			product =
				(float)((long double)shade[i] * brightness[i]);
			result[i] = (float)((long double)product * rgb[i]);
		} else {
			/* Cached 128 * brightness. */
			product = (float)((long double)TEXTURED_BASE *
				brightness[i]);
			result[i] = (float)((long double)product * shade[i]);
		}
	}

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_light_vertex_color(const unsigned char rgb[3],
	const float brightness[3], const float shade[3], int dim,
	float color[3], struct mh_error *error_out)
{
	float result[3], base, light;
	unsigned int i;

	if (rgb == NULL || brightness == NULL || shade == NULL ||
		color == NULL || (dim != 0 && dim != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software vertex material");

	for (i = 0; i < 3; i++) {
		if (!(brightness[i] >= 0 &&
			brightness[i] <= MH_SOFTWARE_BRIGHTNESS_MAX) ||
			!(shade[i] >= 0 && shade[i] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software vertex shade");

		/* Base first, unlike type 0/1. */
		base = (float)((long double)rgb[i] * brightness[i]);
		light = shade[i];

		if (dim)
			light = (float)((long double)light * DETAIL_DIM);

		result[i] = (float)((long double)base * light);
	}

	memcpy(color, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
