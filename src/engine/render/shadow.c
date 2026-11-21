#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/shadow.h"

#define COORDINATE_LIMIT 1000000
#define DISTANCE_SQUARED_LIMIT 32400
#define DISTANCE_FADE_SCALE .007
#define FADE_LIMIT 227
#define GROUND_NORMAL_MIN .8
#define GROUND_OFFSET .04
#define INNER_TRANSMISSION 100
#define EDGE_TRANSMISSION 220
#define TRANSMISSION_MAX 255

enum inner_point {
	FRONT_LEFT = 5,
	FRONT_RIGHT = 6,
	REAR_LEFT = 9,
	REAR_RIGHT = 10
};

const unsigned char mh_shadow_quads[MH_SHADOW_QUADS][4] = {{5, 4, 0, 1},
	{6, 5, 1, 2}, {6, 2, 3, 7}, {5, 9, 8, 4}, {5, 6, 10, 9}, {10, 6, 7, 11},
	{9, 13, 12, 8}, {9, 10, 14, 13}, {10, 11, 15, 14}};
const float mh_shadow_default_points[MH_SHADOW_POINTS * 3] = {-.85f, 0, 1.85f,
	-.5f, 0, 2.05f, .5f, 0, 2.05f, .85f, 0, 1.85f, -1, 0, 1.15f, -.5f, 0,
	1.15f, .5f, 0, 1.15f, 1, 0, 1.15f, -1.05f, 0, -1.15f, -.5f, 0, -1.15f,
	.5f, 0, -1.15f, 1.05f, 0, -1.15f, -.85f, 0, -2, -.5f, 0, -2.1f, .5f, 0,
	-2.1f, .85f, 0, -2};

static int
valid_coordinates(const float *values, unsigned int count)
{
	unsigned int i;

	if (values == NULL)
		return 0;

	for (i = 0; i < count; i++)
		if (!(values[i] >= -COORDINATE_LIMIT &&
			values[i] <= COORDINATE_LIMIT))
			return 0;

	return 1;
}

static void
transform_point(const float *point, const float *matrix, float output[3])
{
	unsigned int j;

	for (j = 0; j < 3; j++)
		output[j] = (float)((long double)point[1] * matrix[3 + j] +
			(long double)point[0] * matrix[j] +
			(long double)point[2] * matrix[6 + j] + matrix[9 + j]);
}

enum mh_error_code
mh_shadow_prepare(const float *points, const float matrix[12],
	const float eye[3], struct mh_shadow_query *output,
	struct mh_error *error_out)
{
	struct mh_shadow_query query;
	float front[3], other[3], delta[3];
	double distance_squared;
	unsigned int j;

	if (points == NULL)
		points = mh_shadow_default_points;

	if (!valid_coordinates(points, MH_SHADOW_POINTS * 3) ||
		!valid_coordinates(matrix, 12) || !valid_coordinates(eye, 3) ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shadow preparation input");

	memset(&query, 0, sizeof(query));
	transform_point(points + FRONT_LEFT * 3, matrix, front);

	for (j = 0; j < 3; j++)
		delta[j] = (float)((long double)front[j] - eye[j]);

	distance_squared = (double)((long double)delta[1] * delta[1] +
		(long double)delta[0] * delta[0] +
		(long double)delta[2] * delta[2]);

	if (distance_squared <= DISTANCE_SQUARED_LIMIT) {
		query.visible = 1;
		query.fade = (float)((long double)distance_squared *
			(long double)DISTANCE_FADE_SCALE);
		transform_point(points + FRONT_RIGHT * 3, matrix, other);

		for (j = 0; j < 3; j++)
			query.probes[0][j] =
				(float)(((long double)front[j] + other[j]) *
					.5L);

		transform_point(
			points + REAR_RIGHT * 3, matrix, query.probes[1]);
		transform_point(
			points + REAR_LEFT * 3, matrix, query.probes[2]);
	}

	*output = query;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_shadow_project(const float *points, const float matrix[12],
	const struct mh_shadow_query *query, const float *contacts,
	struct mh_shadow_shape *output, struct mh_error *error_out)
{
	struct mh_shadow_shape shape;
	float a[3], b[3], normal[3], plane, inverse, world[3];
	double squared_length;
	long double scale, amount;
	unsigned int i, j, value;

	if (points == NULL)
		points = mh_shadow_default_points;

	if (!valid_coordinates(points, MH_SHADOW_POINTS * 3) ||
		!valid_coordinates(matrix, 12) ||
		!valid_coordinates(contacts, MH_SHADOW_PROBES * 3) ||
		query == NULL || output == NULL ||
		!(query->fade >= 0 && query->fade <= FADE_LIMIT) ||
		(query->visible != 0 && query->visible != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shadow projection input");

	memset(&shape, 0, sizeof(shape));

	if (!query->visible) {
		*output = shape;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (j = 0; j < 3; j++) {
		a[j] = (float)((long double)contacts[3 + j] - contacts[j]);
		b[j] = (float)((long double)contacts[6 + j] - contacts[j]);
	}

	for (j = 0; j < 3; j++)
		normal[j] =
			(float)((long double)a[(j + 1) % 3] * b[(j + 2) % 3] -
				(long double)a[(j + 2) % 3] * b[(j + 1) % 3]);

	squared_length = (double)((long double)normal[1] * normal[1] +
		(long double)normal[0] * normal[0] +
		(long double)normal[2] * normal[2]);

	if (squared_length != 0) {
		/*
		 * C89 has no sqrtl. Explicit double sqrt matches other kernels;
		 * extended-precision square root can differ before stores.
		 */
		scale = 1 / (long double)sqrt(squared_length);

		for (j = 0; j < 3; j++)
			normal[j] = (float)(normal[j] * scale);
	}

	if ((double)normal[1] < GROUND_NORMAL_MIN || normal[1] == 0) {
		*output = shape;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	plane = (float)(-((long double)normal[1] * contacts[1] +
		(long double)normal[0] * contacts[0] +
		(long double)normal[2] * contacts[2]));
	inverse = (float)(1 / (long double)normal[1]);

	for (i = 0; i < MH_SHADOW_POINTS; i++) {
		transform_point(points + i * 3, matrix, world);
		shape.points[i][0] = world[0];
		shape.points[i][2] = world[2];
		shape.points[i][1] =
			(float)(-((long double)normal[0] * world[0] + plane +
					(long double)normal[2] * world[2]) *
					inverse +
				(float)GROUND_OFFSET);
		amount = (long double)query->fade +
			((i == FRONT_LEFT || i == FRONT_RIGHT ||
				i == REAR_LEFT || i == REAR_RIGHT)
					? INNER_TRANSMISSION
					: EDGE_TRANSMISSION);
		value = (unsigned int)amount;
		shape.transmission[i] = (unsigned char)(value > TRANSMISSION_MAX
				? TRANSMISSION_MAX
				: value);
	}

	shape.visible = 1;
	*output = shape;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
