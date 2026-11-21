#include <math.h>

#include "engine/core/rotation.h"

#define ORTHONORMAL_TOLERANCE 0.000001
/* Native validity bound, not a clamp or an original physics constant. */
#define ACCUMULATED_TOLERANCE .001

int
mh_rotation_valid_with_error(const double rotation[9], double max_error)
{
	unsigned int i, j, k;
	double dot, determinant, component_limit;

	if (!(max_error >= 0 && max_error < 1))
		return 0;

	component_limit = 1 + max_error;

	for (i = 0; i < 9; i++)
		if (!(rotation[i] >= -component_limit &&
			rotation[i] <= component_limit))
			return 0;

	for (i = 0; i < 3; i++) {
		for (j = i; j < 3; j++) {
			dot = 0;

			for (k = 0; k < 3; k++)
				dot += rotation[i * 3 + k] *
					rotation[j * 3 + k];

			if (fabs(dot - (i == j ? 1 : 0)) > max_error)
				return 0;
		}
	}

	determinant = rotation[0] *
			(rotation[4] * rotation[8] -
				rotation[7] * rotation[5]) -
		rotation[3] *
			(rotation[1] * rotation[8] -
				rotation[7] * rotation[2]) +
		rotation[6] *
			(rotation[1] * rotation[5] - rotation[4] * rotation[2]);

	return fabs(determinant - 1) <= max_error;
}

int
mh_rotation_valid(const double rotation[9])
{
	return mh_rotation_valid_with_error(rotation, ORTHONORMAL_TOLERANCE);
}

int
mh_rotation_valid_accumulated(const double rotation[9])
{
	return mh_rotation_valid_with_error(rotation, ACCUMULATED_TOLERANCE);
}

/* Postmultiply local X, Y, then Z; the Y column pair is (Z,X). */
void
mh_rotation_turn(double rotation[9], const double angles[3])
{
	unsigned int axis, row, first_column, second_column;
	double cosine, sine, first, second;

	for (axis = 0; axis < 3; axis++) {
		cosine = cos(angles[axis]);
		sine = sin(angles[axis]);
		first_column = ((axis + 1) % 3) * 3;
		second_column = ((axis + 2) % 3) * 3;

		for (row = 0; row < 3; row++) {
			first = rotation[first_column + row];
			second = rotation[second_column + row];
			rotation[first_column + row] =
				cosine * first + sine * second;
			rotation[second_column + row] =
				cosine * second - sine * first;
		}
	}
}

/* Ordinary nondegenerate rotation path. Input already validated. */
void
mh_rotation_normalize(double rotation[9])
{
	double length;
	unsigned int i;

	length = sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1] +
		rotation[2] * rotation[2]);

	for (i = 0; i < 3; i++)
		rotation[i] /= length;

	rotation[6] = rotation[1] * rotation[5] - rotation[2] * rotation[4];
	rotation[7] = rotation[2] * rotation[3] - rotation[0] * rotation[5];
	rotation[8] = rotation[0] * rotation[4] - rotation[1] * rotation[3];
	length = sqrt(rotation[6] * rotation[6] + rotation[7] * rotation[7] +
		rotation[8] * rotation[8]);

	for (i = 6; i < 9; i++)
		rotation[i] /= length;

	rotation[3] = rotation[7] * rotation[2] - rotation[8] * rotation[1];
	rotation[4] = rotation[8] * rotation[0] - rotation[6] * rotation[2];
	rotation[5] = rotation[6] * rotation[1] - rotation[7] * rotation[0];
}
