#include <math.h>

#include "engine/core/error.h"
#include "engine/simulation/ai_path.h"

#define POSITION_LIMIT 1e6
#define LOCATE_RADIUS 666

static double
distance_to(const struct mh_ai_point *point, double x, double z)
{
	double dx, dz;

	dx = x - point->x;
	dz = z - point->z;

	return sqrt(dx * dx + dz * dz);
}

/* Compare distances in long double using single-precision path coordinates. */
static long double
follow_distance_squared(const struct mh_ai_point *point, float x, float z)
{
	long double dx, dz;

	dx = (long double)x - (float)point->x;
	dz = (long double)z - (float)point->z;

	return dx * dx + dz * dz;
}

enum mh_error_code
mh_ai_follow(const struct mh_ai_path *path, double x, double z, size_t point,
	size_t corner, struct mh_ai_cursor *output, struct mh_error *error_out)
{
	struct mh_ai_cursor next;
	size_t i, neighbor;
	unsigned int direction;
	float best;
	long double distance;

	if (path == NULL || path->points == NULL || path->count < 2 ||
		path->count > MH_AI_MAX_POINTS || point >= path->count ||
		corner >= path->count || output == NULL ||
		!(x >= -POSITION_LIMIT && x <= POSITION_LIMIT) ||
		!(z >= -POSITION_LIMIT && z <= POSITION_LIMIT)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid incremental AI location query");
	}

	next.point = point;
	next.corner = corner;
	best = (float)follow_distance_squared(
		&path->points[point], (float)x, (float)z);

	/* Finish the forward walk before walking backward. */
	for (direction = 0; direction < 2; direction++) {
		for (i = 0; i < path->count; i++) {
			if (direction == 0)
				neighbor = next.point + 1 == path->count
					? 0
					: next.point + 1;
			else
				neighbor = next.point == 0 ? path->count - 1
							: next.point - 1;

			distance = follow_distance_squared(
				&path->points[neighbor], (float)x, (float)z);

			if (distance >= best)
				break;

			if (next.corner == next.point)
				next.corner = neighbor;

			next.point = neighbor;
			best = (float)distance;
		}

		if (i == path->count)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"AI path walk did not converge");
	}

	next.distance =
		distance_to(&path->points[next.point], (float)x, (float)z);
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_locate(const struct mh_ai_path *path, double x, double z,
	struct mh_ai_location *output, struct mh_error *error_out)
{
	struct mh_ai_location result;
	double distance, best_distance;
	size_t i;

	if (path == NULL || path->points == NULL || path->count < 2 ||
		path->count > MH_AI_MAX_POINTS || output == NULL ||
		!(x >= -POSITION_LIMIT && x <= POSITION_LIMIT) ||
		!(z >= -POSITION_LIMIT && z <= POSITION_LIMIT)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI location query");
	}

	result.point = 0;
	result.in_range = 0;
	best_distance = LOCATE_RADIUS;

	for (i = 0; i < path->count; i++) {
		distance = distance_to(&path->points[i], x, z);

		if (distance < best_distance) {
			/*
			 * Original compares before its store of the rounded
			 * best value.
			 */
			best_distance = (double)(float)distance;
			result.point = i;
			result.in_range = 1;
		}
	}

	result.distance = distance_to(&path->points[result.point], x, z);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
