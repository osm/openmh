#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/route_projection.h"

#define KEY_COUNT_LIMIT 100000
#define POSITION_LIMIT 1e6
#define TIME_LIMIT 1e9
#define DOUBLE_DISTANCE_INITIAL 1e30
#define MIN_EDGE_SQUARED 1e-20

static int
valid_key(const struct mh_scene_key *key)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		if (!(key->value[i] >= -POSITION_LIMIT &&
			key->value[i] <= POSITION_LIMIT))
			return 0;

	return key->time >= 0 && key->time <= TIME_LIMIT;
}

/*
 * Each axis multiplies the extended difference by its binary32 spill, then
 * stores the square. The final sum stays extended for comparison.
 */
static long double
cached_distance_squared(
	const struct mh_scene_key *key, const float query_position[3])
{
	float squared[3], xy;
	long double difference;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		difference =
			(long double)(float)key->value[i] - query_position[i];
		squared[i] = (float)(difference * (float)difference);
	}

	xy = (float)((long double)squared[0] + squared[1]);

	return (long double)xy + squared[2];
}

static enum mh_error_code
find_nearest_key(const struct mh_route *route, const float query_position[3],
	unsigned long cache, size_t *output, struct mh_error *error_out)
{
	const struct mh_scene_key *keys;
	size_t count, index, candidate, previous, step, i;
	float best_squared, rounded_squared;
	long double distance_squared, current_squared, difference;
	unsigned int axis;

	keys = route->motion.keys;
	count = route->motion.key_count - 1;

	if (cache == MH_ROUTE_CACHE_EMPTY) {
		/* No intermediate binary32 differences/squares here. */
		index = 0;
		best_squared = FLT_MAX;

		for (i = 0; i < count; i++) {
			if (!valid_key(keys + i))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid route projection key");

			distance_squared = 0;

			for (axis = 0; axis < 3; axis++) {
				difference = (long double)(float)keys[i]
						.value[axis] -
					query_position[axis];
				distance_squared += difference * difference;
			}

			rounded_squared = (float)distance_squared;

			if (rounded_squared < best_squared) {
				best_squared = rounded_squared;
				index = i;
			}
		}

		*output = index;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	index = cache % count;
	previous = count;

	if (!valid_key(keys + index))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route projection key");

	best_squared =
		(float)cached_distance_squared(keys + index, query_position);

	for (step = 0; step < count * 2; step++) {
		candidate = (index + 1) % count;

		if (!valid_key(keys + candidate))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid route projection key");

		distance_squared = cached_distance_squared(
			keys + candidate, query_position);

		if (distance_squared > best_squared) {
			candidate = (index + count - 1) % count;

			if (!valid_key(keys + candidate))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid route projection key");

			distance_squared = cached_distance_squared(
				keys + candidate, query_position);

			if (distance_squared >= best_squared) {
				*output = index;
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}
		}

		/*
		 * Rounded stored distances can accept both directions between
		 * adjacent keys. The original loops forever on these queries;
		 * retain the closer key using the unrounded comparison instead.
		 */
		if (candidate == previous) {
			current_squared = cached_distance_squared(
				keys + index, query_position);

			if (distance_squared != current_squared ||
				distance_squared < best_squared) {
				*output = distance_squared < current_squared
					? candidate
					: index;
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}
		}

		previous = index;
		index = candidate;
		best_squared = (float)distance_squared;
	}

	/* Degenerate equal-distance cycles can hang the original walk. */

	return mh_error_set(error_out, MH_ERROR_FAILED,
		"route projection cache did not converge");
}

static int
project_segment_float(const float query_position[3],
	const struct mh_scene_key *a, const struct mh_scene_key *b,
	float *fraction)
{
	float edge[3], offset[3];
	long double length_squared, numerator;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		edge[i] = (float)((long double)(float)b->value[i] -
			(float)a->value[i]);
		offset[i] = (float)((long double)query_position[i] -
			(float)a->value[i]);
	}

	length_squared = ((long double)edge[1] * edge[1] +
				(long double)edge[0] * edge[0]) +
		(long double)edge[2] * edge[2];

	if (!(length_squared > 0))
		return 0;

	numerator = ((long double)edge[1] * offset[1] +
			(long double)edge[0] * offset[0]) +
		(long double)edge[2] * offset[2];
	numerator /= length_squared;

	if (!(numerator >= -FLT_MAX && numerator <= FLT_MAX))
		return 0;

	*fraction = (float)numerator;

	return *fraction >= 0 && *fraction <= 1;
}

enum mh_error_code
mh_route_project_cached(const struct mh_route *route, const double position[3],
	unsigned long cache, struct mh_route_projection *output,
	struct mh_error *error_out)
{
	const struct mh_scene_key *keys;
	struct mh_route_projection result;
	size_t count, index = 0, before, after, low, high, middle;
	unsigned int i;
	float query_position[3], period, parameter, start, end, fraction, span,
		channels[4];
	enum mh_error_code status;

	if (route == NULL || route->motion.keys == NULL ||
		route->motion.key_count < 3 ||
		route->motion.key_count > KEY_COUNT_LIMIT || position == NULL ||
		output == NULL || cache > MH_ROUTE_CACHE_EMPTY)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid cached route projection");

	keys = route->motion.keys;
	count = route->motion.key_count - 1;

	if (keys[0].time != 0 ||
		!(keys[count].time > 0 && keys[count].time <= TIME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route projection period");

	period = (float)keys[count].time;

	if (!(period > 0))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"route projection period collapses to zero");

	for (i = 0; i < 3; i++) {
		if (!(position[i] >= -POSITION_LIMIT &&
			position[i] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid route projection position");

		query_position[i] = (float)position[i];
	}

	status = find_nearest_key(
		route, query_position, cache, &index, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	before = (index + count - 1) % count;
	after = (index + 1) % count;

	if (!valid_key(keys + before) || !valid_key(keys + after))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route projection neighbor");

	if (project_segment_float(
		query_position, keys + before, keys + index, &fraction)) {
		start = (float)keys[before].time;

		if (index == 0)
			start = (float)((long double)start - period);

		parameter = (float)((long double)start +
			((long double)(float)keys[index].time - start) *
				fraction);
	} else if (project_segment_float(query_position, keys + index,
			keys + after, &fraction)) {
		end = after == 0 ? period : (float)keys[after].time;
		start = (float)keys[index].time;
		parameter =
			(float)(((long double)end - start) * fraction + start);
	} else
		parameter = (float)keys[index].time;

	if (parameter < 0)
		parameter = (float)((long double)parameter + period);

	/*
	 * The forced-linear sampler still has the full repeat period, but only
	 * the retained keys. The last segment therefore holds its first point.
	 */
	if (parameter >= period)
		parameter = (float)((long double)parameter - period);

	low = 0;
	high = count - 1;

	while (high - low > 1) {
		middle = low + (high - low) / 2;

		if ((float)keys[middle].time <= parameter)
			low = middle;
		else
			high = middle;
	}

	if (parameter >= (float)keys[count - 1].time)
		low = count - 1;

	if (!valid_key(keys + low))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route projection sample");

	if (low == count - 1 || parameter == 0) {
		for (i = 0; i < 4; i++)
			channels[i] =
				(float)keys[parameter == 0 ? 0 : low].value[i];
	} else {
		if (!valid_key(keys + low + 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid route projection sample");

		span = (float)((long double)(float)keys[low + 1].time -
			(float)keys[low].time);

		if (!(span > 0))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"route projection times collapse at binary32 precision");

		fraction = (float)((float)((long double)parameter -
					(float)keys[low].time) /
			(long double)span);

		for (i = 0; i < 4; i++)
			channels[i] =
				(float)(((long double)(float)keys[low + 1]
							.value[i] -
						(float)keys[low].value[i]) *
						fraction +
					(float)keys[low].value[i]);
	}

	memset(&result, 0, sizeof(result));
	result.nearest_key = index;
	result.parameter = parameter;

	for (i = 0; i < 3; i++)
		result.position[i] = channels[i];

	result.sound_blend = channels[3];
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Native double projection used by placement and inspection. */
static double
dot_double(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int
valid_double_route(const struct mh_route *route)
{
	return route != NULL && route->motion.keys != NULL &&
		route->motion.key_count >= 3 &&
		route->motion.key_count <= KEY_COUNT_LIMIT;
}

static double
wrap_double(const struct mh_route *route, double time)
{
	double period;

	period = route->motion.keys[route->motion.key_count - 1].time;
	time = fmod(time, period);

	return time < 0 ? time + period : time;
}

static int
project_segment_double(const double position[3], const struct mh_scene_key *a,
	const struct mh_scene_key *b, double *fraction)
{
	double edge[3], offset[3], length_squared;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		edge[i] = b->value[i] - a->value[i];
		offset[i] = position[i] - a->value[i];
	}

	length_squared = dot_double(edge, edge);

	if (!(length_squared > MIN_EDGE_SQUARED))
		return 0;

	*fraction = dot_double(edge, offset) / length_squared;

	return *fraction >= 0 && *fraction <= 1;
}

enum mh_error_code
mh_route_project(const struct mh_route *route, const double position[3],
	struct mh_route_projection *output, struct mh_error *error_out)
{
	const struct mh_scene_key *keys;
	struct mh_route_projection result;
	size_t i, count, nearest_index, previous, next, segment;
	unsigned int axis;
	double difference[3], best_squared, distance_squared, time, fraction,
		start, end;

	if (!valid_double_route(route) || position == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recovery arguments");

	for (axis = 0; axis < 3; axis++)
		if (!(position[axis] >= -POSITION_LIMIT &&
			position[axis] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid recovery position");

	keys = route->motion.keys;
	count = route->motion.key_count - 1;
	nearest_index = 0;
	best_squared = DOUBLE_DISTANCE_INITIAL;

	for (i = 0; i < count; i++) {
		for (axis = 0; axis < 3; axis++)
			difference[axis] = keys[i].value[axis] - position[axis];

		distance_squared = dot_double(difference, difference);

		if (distance_squared < best_squared) {
			best_squared = distance_squared;
			nearest_index = i;
		}
	}

	previous = nearest_index == 0 ? count - 1 : nearest_index - 1;
	next = nearest_index + 1 == count ? 0 : nearest_index + 1;

	if (project_segment_double(position, keys + previous,
		keys + nearest_index, &fraction)) {
		start = keys[previous].time -
			(nearest_index == 0 ? keys[count].time : 0);
		time = start + (keys[nearest_index].time - start) * fraction;
	} else if (project_segment_double(position, keys + nearest_index,
			keys + next, &fraction)) {
		end = next == 0 ? keys[count].time : keys[next].time;
		time = keys[nearest_index].time +
			(end - keys[nearest_index].time) * fraction;
	} else
		time = keys[nearest_index].time;

	time = wrap_double(route, time);
	memset(&result, 0, sizeof(result));
	result.parameter = time;
	result.nearest_key = nearest_index;

	/*
	 * Temporarily omit the duplicate closing key and force linear sampling.
	 * Beyond the last retained time, hold that key. Restore the full curve
	 * for the direction query.
	 */
	segment = 0;

	while (segment + 1 < count && keys[segment + 1].time <= time)
		segment++;

	fraction = segment + 1 == count ? 0
					: (time - keys[segment].time) /
			(keys[segment + 1].time - keys[segment].time);

	for (axis = 0; axis < 3; axis++)
		result.position[axis] = keys[segment].value[axis] +
			fraction *
				(keys[segment + 1].value[axis] -
					keys[segment].value[axis]);

	result.sound_blend = keys[segment].value[3] +
		fraction *
			(keys[segment + 1].value[3] - keys[segment].value[3]);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
