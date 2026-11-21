#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/body_points.h"

#define COORDINATE_LIMIT 10000
#define BASE_SUPPORT_MIN 3
#define UPPER_POINT_HEIGHT 1.3

enum {
	UPPER_FRONT = MH_CAR_BASE_POINTS,
	UPPER_RIGHT,
	UPPER_LEFT
};

unsigned int
mh_body_point_count(unsigned int supported_wheels, unsigned int role)
{
	return supported_wheels < BASE_SUPPORT_MIN && role == 0
		? (unsigned int)MH_CAR_BODY_POINTS
		: (unsigned int)MH_CAR_BASE_POINTS;
}

enum mh_error_code
mh_car_body_points(const struct mh_car *car,
	double points[MH_CAR_BODY_POINTS][3], struct mh_error *error_out)
{
	double result[MH_CAR_BODY_POINTS][3];
	unsigned int axis, point;

	if (car == NULL || points == NULL || !car->has_corners)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing car body points");

	for (axis = 0; axis < 3; axis++) {
		if (!(car->center_mass[axis] >= -COORDINATE_LIMIT &&
			car->center_mass[axis] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body center");

		for (point = 0; point < MH_CAR_BASE_POINTS; point++)
			if (!(car->corners[point][axis] >= -COORDINATE_LIMIT &&
				car->corners[point][axis] <=
					COORDINATE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid body corner");
	}

	for (point = 0; point < MH_CAR_BASE_POINTS; point++) {
		memcpy(result[point], car->corners[point],
			sizeof(result[point]));
		result[point][1] = car->center_mass[1];
	}

	result[UPPER_FRONT][0] = 0;
	result[UPPER_FRONT][2] = 1;
	result[UPPER_RIGHT][0] = 1;
	result[UPPER_RIGHT][2] = -1;
	result[UPPER_LEFT][0] = -1;
	result[UPPER_LEFT][2] = -1;

	for (point = MH_CAR_BASE_POINTS; point < MH_CAR_BODY_POINTS; point++)
		result[point][1] = (double)(float)UPPER_POINT_HEIGHT;

	memcpy(points, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
