#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/render/software_shadow.h"

#define COORDINATE_LIMIT 1000000
#define GROUND_NORMAL_MIN .8

enum inner_point {
	FRONT_LEFT = 5,
	FRONT_RIGHT = 6,
	REAR_LEFT = 9,
	REAR_RIGHT = 10
};

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

/* Use the software scene's binary32 arithmetic precision. */
static void
transform(const float point[3], const float matrix[12], float output[3])
{
	float x, y, z, sum;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		y = (float)((double)point[1] * matrix[3 + i]);
		x = (float)((double)point[0] * matrix[i]);
		z = (float)((double)point[2] * matrix[6 + i]);
		sum = (float)((double)y + x);
		sum = (float)((double)sum + z);
		output[i] = (float)((double)sum + matrix[9 + i]);
	}
}

enum mh_error_code
mh_software_shadow_prepare(const float *points, const float matrix[12],
	struct mh_software_shadow_query *output, struct mh_error *error_out)
{
	struct mh_software_shadow_query query;
	float front[3], other[3], probes[MH_SHADOW_PROBES][3], sum;
	unsigned int i, j;

	if (points == NULL)
		points = mh_shadow_default_points;

	if (!valid_coordinates(points, MH_SHADOW_POINTS * 3) ||
		!valid_coordinates(matrix, 12) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software shadow preparation input");

	transform(points + FRONT_LEFT * 3, matrix, front);
	transform(points + FRONT_RIGHT * 3, matrix, other);

	for (j = 0; j < 3; j++) {
		sum = (float)((double)front[j] + other[j]);
		probes[0][j] = (float)((double)sum * .5);
	}

	transform(points + REAR_RIGHT * 3, matrix, probes[1]);
	transform(points + REAR_LEFT * 3, matrix, probes[2]);

	for (i = 0; i < MH_SHADOW_PROBES; i++) {
		for (j = 0; j < 3; j++)
			query.rays[i][0][j] = query.rays[i][1][j] =
				probes[i][j];

		query.rays[i][0][1] = (float)((double)probes[i][1] - 10);
		query.rays[i][1][1] = (float)((double)probes[i][1] + 1);
	}

	*output = query;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
ground_plane(const float contacts[9], float normal[3], float *plane)
{
	float a[3], b[3], first, second, square[3], sum, length, inverse;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		a[i] = (float)((double)contacts[3 + i] - contacts[i]);
		b[i] = (float)((double)contacts[6 + i] - contacts[i]);
	}

	for (i = 0; i < 3; i++) {
		first = (float)((double)a[(i + 1) % 3] * b[(i + 2) % 3]);
		second = (float)((double)a[(i + 2) % 3] * b[(i + 1) % 3]);
		normal[i] = (float)((double)first - second);
		square[i] = (float)((double)normal[i] * normal[i]);
	}

	sum = (float)((double)square[1] + square[0]);
	sum = (float)((double)sum + square[2]);

	if (sum == 0)
		return 0;

	length = (float)sqrt((double)sum);
	inverse = (float)(1.0 / length);

	for (i = 0; i < 3; i++)
		normal[i] = (float)((double)normal[i] * inverse);

	if ((double)normal[1] < GROUND_NORMAL_MIN)
		return 0;

	first = (float)((double)normal[1] * contacts[1]);
	second = (float)((double)normal[0] * contacts[0]);
	sum = (float)((double)first + second);
	first = (float)((double)normal[2] * contacts[2]);
	*plane = -(float)((double)sum + first);

	return 1;
}

enum mh_error_code
mh_software_shadow_project(const float *points, const float matrix[12],
	const float contacts[9], struct mh_software_shadow_shape *output,
	struct mh_error *error_out)
{
	struct mh_software_shadow_shape shape;
	float normal[3], plane, inverse, first, second, sum;
	unsigned int i;

	if (points == NULL)
		points = mh_shadow_default_points;

	if (!valid_coordinates(points, MH_SHADOW_POINTS * 3) ||
		!valid_coordinates(matrix, 12) ||
		!valid_coordinates(contacts, MH_SHADOW_PROBES * 3) ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software shadow projection input");

	memset(&shape, 0, sizeof(shape));

	if (!ground_plane(contacts, normal, &plane)) {
		*output = shape;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	inverse = (float)(1.0 / normal[1]);

	for (i = 0; i < MH_SHADOW_POINTS; i++) {
		transform(points + i * 3, matrix, shape.points[i]);
		first = (float)((double)normal[0] * shape.points[i][0]);
		sum = (float)((double)first + plane);
		second = (float)((double)normal[2] * shape.points[i][2]);
		sum = -(float)((double)sum + second);
		shape.points[i][1] = (float)((double)sum * inverse);
	}

	shape.visible = 1;
	*output = shape;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
