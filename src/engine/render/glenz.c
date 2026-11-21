#include <float.h>
#include <math.h>
#include <stddef.h>

#include "engine/render/glenz.h"
#include "engine/render/material_color.h"

#define COORDINATE_LIMIT 1000000
#define REFLECTION_RADIUS_SQUARED 30.25f
#define REFLECTION_X_SCALE 0.1818181872367859f
#define MAP_SCALE 0.19449999928474426f
#define SCROLL_SCALE 0.03
#define SCROLL_PERIOD 100.0f
#define V_LIMIT 5.0f
#define HALF_PI 1.570796327
#define ANGLE_CUBIC 0.16666666
#define ANGLE_QUINTIC 0.075
#define ANGLE_SEPTIC 0.44642857
#define PHONG_NORMAL_SCALE 0.24950000643730164f
#define PHONG_SCREEN_SCALE 0.2495

static int
bounded(const float *values, unsigned int count)
{
	unsigned int index;

	if (values == NULL)
		return 0;

	for (index = 0; index < count; index++)
		if (!(values[index] >= -COORDINATE_LIMIT &&
			values[index] <= COORDINATE_LIMIT))
			return 0;

	return 1;
}

enum mh_error_code
mh_glenz_car_color(const unsigned char rgb[3], const float brightness[3],
	const float light[3], float color[3], struct mh_error *error_out)
{
	if (rgb == NULL || color == NULL || !bounded(brightness, 3) ||
		!bounded(light, 3))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Glenz car color");

	return mh_material_color(MH_MATERIAL_ENVIRONMENT, MH_LIGHT_CAR, rgb,
		brightness, light, color, error_out);
}

enum mh_error_code
mh_glenz_scroll_update(struct mh_glenz_scroll *scroll, const float position[3],
	const float forward[3], struct mh_error *error_out)
{
	struct mh_glenz_scroll next;
	float delta[3], square[3], distance_squared;
	long double difference, direction, step;
	unsigned int axis;

	if (scroll == NULL || !bounded(position, 3) || !bounded(forward, 3) ||
		!bounded(scroll->previous, 3) || !bounded(&scroll->phase, 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Glenz scroll input");

	for (axis = 0; axis < 3; axis++) {
		difference =
			(long double)position[axis] - scroll->previous[axis];
		delta[axis] = (float)difference;
		square[axis] = (float)(difference * delta[axis]);
		next.previous[axis] = position[axis];
	}

	distance_squared =
		(float)(((long double)square[0] + square[1]) + square[2]);
	direction = ((long double)delta[1] * forward[1] +
			(long double)delta[0] * forward[0]) +
		(long double)delta[2] * forward[2];
	step = sqrt(sqrt((double)distance_squared));
	next.phase = (float)(direction > 0 ? (long double)scroll->phase + step
					: (long double)scroll->phase - step);

	if (next.phase > SCROLL_PERIOD)
		next.phase = (float)((long double)next.phase - SCROLL_PERIOD);
	else if (next.phase < -SCROLL_PERIOD)
		next.phase = (float)((long double)next.phase + SCROLL_PERIOD);

	*scroll = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Preserve the polynomial and coefficient precision. */
static long double
reflection_angle(long double x)
{
	long double power, angle;

	power = x * x * x;
	angle = x + power * (long double)ANGLE_CUBIC;
	power = (power * x) * x;
	angle += power * (long double)ANGLE_QUINTIC;
	power = (power * x) * x;

	return (long double)HALF_PI -
		(angle + power * (long double)ANGLE_SEPTIC);
}

/* Retained Y/Z intermediates matter: the original stores without popping. */
static long double
reflect(const float position[3], const float normal[3], const float eye[3],
	float reflected[3], long double *z)
{
	float view[3];
	long double twice_dot, y;
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		view[axis] = (float)((long double)eye[axis] - position[axis]);

	twice_dot = ((long double)view[1] * normal[1] +
			(long double)view[0] * normal[0]) +
		(long double)view[2] * normal[2];
	twice_dot *= 2;
	reflected[0] = (float)(twice_dot * normal[0] - view[0]);
	y = twice_dot * normal[1] - view[1];
	reflected[1] = (float)y;
	*z = twice_dot * normal[2] - view[2];
	reflected[2] = (float)*z;

	return ((long double)reflected[0] * reflected[0] + y * reflected[1]) +
		(*z * .5L) * reflected[2];
}

enum mh_error_code
mh_glenz_reflection_uv(const float position[3], const float normal[3],
	const float eye[3], float scroll, float uv[2],
	struct mh_error *error_out)
{
	float reflected[3], linear, discriminant, distance, output[2];
	long double square, inverse, z, dot, coefficient, constant, x, value;

	if (!bounded(position, 3) || !bounded(normal, 3) || !bounded(eye, 3) ||
		!(scroll >= -COORDINATE_LIMIT && scroll <= COORDINATE_LIMIT) ||
		uv == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Glenz reflection input");

	square = reflect(position, normal, eye, reflected, &z);

	if (!(square > 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"degenerate Glenz reflection vector");

	inverse = 1 / square;
	dot = ((long double)reflected[0] * position[0] +
		(long double)reflected[1] * position[1]) +
		(z * .5L) * position[2];
	coefficient = (inverse * 2) * dot;
	constant = (((long double)position[1] * position[1] +
			(long double)position[0] * position[0]) +
			((long double)position[2] * .5L) * position[2]) -
		REFLECTION_RADIUS_SQUARED;

	/* Use coefficient/4, not coefficient squared/4. */
	value = coefficient * .25L - constant * inverse;

	if (!(coefficient >= -FLT_MAX && coefficient <= FLT_MAX) ||
		!(value >= -FLT_MAX && value <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"Glenz reflection discriminant outside float range");

	linear = (float)coefficient;
	discriminant = (float)value;
	output[0] = output[1] = scroll;

	if (discriminant >= 0) {
		distance = (float)((long double)linear * -.5L +
			(long double)sqrt((double)discriminant));

		if (distance >= 0) {
			x = -((long double)distance * reflected[0] +
				position[0]) *
				REFLECTION_X_SCALE;
			value = reflection_angle(x) * MAP_SCALE + MAP_SCALE;

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"Glenz reflection coordinate outside float range");

			output[0] = (float)value;
			output[1] =
				(float)(((long double)distance * reflected[2] +
						position[2]) *
					MAP_SCALE);

			if (output[1] > V_LIMIT)
				output[1] = V_LIMIT;
			else if (output[1] < -V_LIMIT)
				output[1] = -V_LIMIT;

			output[1] = (float)((long double)scroll * SCROLL_SCALE +
				output[1]);
		}
	}

	uv[0] = output[0];
	uv[1] = output[1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_glenz_phong_uv(const float normal[3], const float screen[2],
	const float center[2], float inverse_half_width, float uv[2],
	struct mh_error *error_out)
{
	float scale, offset[2], output[2];
	unsigned int axis;

	if (!bounded(normal, 3) || !bounded(screen, 2) || !bounded(center, 2) ||
		!(inverse_half_width > 0 && inverse_half_width <= 1) ||
		uv == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Glenz Phong input");

	scale = (float)((long double)inverse_half_width * PHONG_SCREEN_SCALE);
	offset[0] = (float)(((long double)screen[0] - center[0]) * scale);
	offset[1] = (float)(((long double)center[1] - screen[1]) * scale);

	for (axis = 0; axis < 2; axis++) {
		output[axis] = (float)(((long double)offset[axis] + .5L) +
			(long double)normal[axis] * PHONG_NORMAL_SCALE);

		if (output[axis] < 0)
			output[axis] = 0;
		else if (output[axis] > 1)
			output[axis] = 1;
	}

	uv[0] = output[0];
	uv[1] = output[1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
