#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/placement.h"
#include "engine/simulation/route_projection.h"

#define KEY_COUNT_LIMIT 100000
#define GRID_SLOT_LIMIT 63
#define GRID_SPACING_LIMIT 100
#define DIRECTION_ATTEMPTS 100
#define DIRECTION_HALF_STEP .5
#define DIRECTION_MIN_SQUARED .25
#define MIN_VECTOR_LENGTH 1e-12
#define MAX_VECTOR_LENGTH 1e9

static double
dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int
normalize(double vector[3])
{
	double length;
	unsigned int i;

	length = sqrt(dot(vector, vector));

	if (!(length > MIN_VECTOR_LENGTH && length < MAX_VECTOR_LENGTH))
		return 0;

	for (i = 0; i < 3; i++)
		vector[i] /= length;

	return 1;
}

static int
valid_route(const struct mh_route *route)
{
	return route != NULL && route->motion.keys != NULL &&
		route->motion.key_count >= 3 &&
		route->motion.key_count <= KEY_COUNT_LIMIT;
}

static double
wrap_frame(const struct mh_route *route, double frame)
{
	double period;

	period = route->motion.keys[route->motion.key_count - 1].time;
	frame = fmod(frame, period);

	return frame < 0 ? frame + period : frame;
}

/*
 * Enlarge a symmetric sample interval until the two positions are at least .5
 * world units apart. No seam tangent smoothing is added.
 */
static enum mh_error_code
route_direction(const struct mh_route *route, double frame, double output[3],
	struct mh_error *error_out)
{
	double before_sample[9], after_sample[9], step;
	unsigned int attempt, i;
	enum mh_error_code status;

	for (attempt = 1; attempt < DIRECTION_ATTEMPTS; attempt++) {
		step = DIRECTION_HALF_STEP * attempt;
		status = mh_scene_channels(&route->motion,
			wrap_frame(route, frame - step), before_sample,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_scene_channels(&route->motion,
			wrap_frame(route, frame + step), after_sample,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 3; i++)
			output[i] = after_sample[i] - before_sample[i];

		if (dot(output, output) >= DIRECTION_MIN_SQUARED &&
			normalize(output))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"route has no usable direction within 99 samples");
}

static enum mh_error_code
orientation_from_forward(
	const double forward[3], double rotation[9], struct mh_error *error_out)
{
	rotation[0] = forward[2];

	rotation[1] = 0;
	rotation[2] = -forward[0];

	if (!normalize(rotation))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"route direction is vertical");

	memcpy(rotation + 6, forward, 3 * sizeof(double));
	rotation[3] = rotation[7] * rotation[2] - rotation[8] * rotation[1];
	rotation[4] = rotation[8] * rotation[0] - rotation[6] * rotation[2];
	rotation[5] = rotation[6] * rotation[1] - rotation[7] * rotation[0];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_placement_grid(const struct mh_track *track, const struct mh_route *route,
	unsigned int slot, struct mh_placement *output,
	struct mh_error *error_out)
{
	struct mh_placement result;
	double forward[3], side[3], start_sample[9];
	double longitudinal_offset, lateral_offset;
	unsigned int i;
	enum mh_error_code status;

	if (track == NULL || !track->has_start_grid || !valid_route(route) ||
		output == NULL || slot > GRID_SLOT_LIMIT) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid grid arguments");
	}

	for (i = 0; i < 4; i++)
		if (!(track->start_grid[i] >= 0 &&
			track->start_grid[i] <= GRID_SPACING_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid grid spacing");

	status = mh_scene_channels(&route->motion, 0, start_sample, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = route_direction(route, 0, forward, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	side[0] = forward[2];
	side[1] = forward[1];
	side[2] = -forward[0];

	if (!normalize(side))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid grid direction");

	longitudinal_offset = -(double)(slot / 2) * track->start_grid[0] -
		track->start_grid[1];

	if (slot & 1)
		longitudinal_offset -= track->start_grid[3];

	lateral_offset =
		(slot & 1) ? track->start_grid[2] : -track->start_grid[2];
	memset(&result, 0, sizeof(result));

	for (i = 0; i < 3; i++)
		result.position[i] = start_sample[i] +
			forward[i] * longitudinal_offset +
			side[i] * lateral_offset;

	status = orientation_from_forward(forward, result.rotation, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_placement_recover(const struct mh_route *route, const double position[3],
	struct mh_placement *output, struct mh_error *error_out)
{
	struct mh_route_projection projected;
	struct mh_placement result;
	double forward[3];
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing recovery output");

	status = mh_route_project(route, position, &projected, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.route_parameter = projected.parameter;
	memcpy(result.position, projected.position, sizeof(result.position));
	status =
		route_direction(route, projected.parameter, forward, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = orientation_from_forward(forward, result.rotation, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
