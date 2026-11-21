#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/route_curve.h"

#define KEY_COUNT_LIMIT 100000
#define FRAME_LIMIT 1e9
#define TCB_LIMIT 1000
#define CHANNEL_LIMIT 1e6
#define SAMPLE_LIMIT 1e30L
#define DIRECTION_ATTEMPTS 100
#define DIRECTION_HALF_STEP .5
#define DIRECTION_MIN_SQUARED .25f

static int
valid_route(const struct mh_route *route)
{
	return route != NULL && route->motion.keys != NULL &&
		route->motion.key_count >= 2 &&
		route->motion.key_count <= KEY_COUNT_LIMIT &&
		route->motion.keys[0].time == 0 &&
		route->motion.keys[route->motion.key_count - 1].time > 0 &&
		route->motion.keys[route->motion.key_count - 1].time <=
		FRAME_LIMIT;
}

static int
valid_key(const struct mh_scene_key *key)
{
	unsigned int i;

	if (!(key->time >= 0 && key->time <= FRAME_LIMIT) ||
		!(key->tension >= -TCB_LIMIT && key->tension <= TCB_LIMIT) ||
		!(key->continuity >= -TCB_LIMIT &&
			key->continuity <= TCB_LIMIT) ||
		!(key->bias >= -TCB_LIMIT && key->bias <= TCB_LIMIT))
		return 0;

	for (i = 0; i < 9; i++)
		if (!(key->value[i] >= -CHANNEL_LIMIT &&
			key->value[i] <= CHANNEL_LIMIT))
			return 0;

	return 1;
}

enum mh_error_code
mh_route_curve_sample(const struct mh_route *route, double frame,
	float output[9], struct mh_error *error_out)
{
	const struct mh_scene_key *keys, *start_key, *end_key, *previous_key,
		*next_key;
	size_t low, high, middle;
	unsigned int i;
	unsigned long cycles;
	float result[9], time, period, span, fraction, out_time_ratio,
		in_time_ratio;
	float basis[4], out_before, out_after, in_before, in_after,
		out_weight_sum, delta, outgoing, incoming;
	long double wide, square, cube;

	if (!valid_route(route) || output == NULL ||
		!(frame >= -FRAME_LIMIT && frame <= FRAME_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route curve sample");

	keys = route->motion.keys;
	period = (float)keys[route->motion.key_count - 1].time;

	if (!(period > 0))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"route period collapses at binary32 precision");

	time = (float)frame;

	/*
	 * Repeat wrapping includes the original binary32 multiplication spill
	 * on the negative-time branch.
	 */
	if (time < 0) {
		wide = -(long double)time / period;

		if (!(wide < (long double)MH_U32_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"route repeat count exceeds bounds");

		cycles = (unsigned long)wide;

		if ((long double)cycles * period != -(long double)time)
			cycles++;

		time = (float)((long double)time +
			(float)((long double)cycles * period));
	}

	if (time >= period) {
		wide = (long double)time / period;

		if (!(wide < MH_U32_MODULUS))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"route repeat count exceeds bounds");

		cycles = (unsigned long)wide;
		time = (float)((long double)time -
			(long double)cycles * period);
	}

	low = 0;
	high = route->motion.key_count - 1;

	while (high - low > 1) {
		middle = low + (high - low) / 2;

		if ((float)keys[middle].time <= time)
			low = middle;
		else
			high = middle;
	}

	start_key = keys + low;
	end_key = start_key + 1;
	previous_key = low == 0 ? start_key : start_key - 1;
	next_key = low + 2 == route->motion.key_count ? end_key : end_key + 1;

	if (!valid_key(start_key) || !valid_key(end_key) ||
		!valid_key(previous_key) || !valid_key(next_key))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid route curve keys");

	if (time == 0) {
		for (i = 0; i < 9; i++)
			result[i] = (float)keys[0].value[i];

		memcpy(output, result, sizeof(result));

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	span = (float)((long double)(float)end_key->time -
		(float)start_key->time);

	if (!(span > 0))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"route keys collapse at binary32 precision");

	fraction = (float)((float)((long double)time - (float)start_key->time) /
		(long double)span);

	if (end_key->linear) {
		for (i = 0; i < 9; i++)
			result[i] =
				(float)(((long double)(float)end_key->value[i] -
						(float)start_key->value[i]) *
						fraction +
					(float)start_key->value[i]);
	} else {
		/*
		 * Round each Hermite coefficient to float after evaluating
		 * its polynomial in long double.
		 */
		square = (long double)fraction * fraction;
		cube = (long double)fraction * square;
		wide = (square * 3 - cube) - cube;
		basis[0] = (float)(1 - wide);
		basis[1] = (float)wide;
		basis[2] = (float)(((cube - square) - square) + fraction);
		basis[3] = (float)(cube - square);
		out_before = (float)((1 -
					(long double)(float)
						start_key->tension) *
			(1 + (long double)(float)start_key->continuity) *
			(1 + (long double)(float)start_key->bias));
		out_after = (float)((1 -
					(long double)(float)
						start_key->tension) *
			(1 - (long double)(float)start_key->continuity) *
			(1 - (long double)(float)start_key->bias));
		in_before = (float)((1 - (long double)(float)end_key->tension) *
			(1 - (long double)(float)end_key->continuity) *
			(1 + (long double)(float)end_key->bias));
		in_after = (float)((1 - (long double)(float)end_key->tension) *
			(1 + (long double)(float)end_key->continuity) *
			(1 - (long double)(float)end_key->bias));
		out_time_ratio = low == 0
			? 0
			: (float)(span /
				((long double)(float)end_key->time -
					(float)previous_key->time));
		in_time_ratio = (float)(span /
			((long double)(float)next_key->time -
				(float)start_key->time));
		out_weight_sum = (float)((long double)out_before + out_after);

		for (i = 0; i < 9; i++) {
			delta = (float)((long double)(float)end_key->value[i] -
				(float)start_key->value[i]);
			outgoing = low == 0
				? (float)((long double)out_weight_sum * .5L *
					delta)
				: (float)((((long double)(float)
							start_key->value[i] -
						(float)previous_key
							->value[i]) *
							out_before +
						(long double)out_after *
							delta) *
					out_time_ratio);

			incoming = (float)((((long double)(float)
							next_key->value[i] -
						(float)end_key->value[i]) *
							in_after +
						(long double)in_before *
							delta) *
				in_time_ratio);
			wide = (long double)(float)start_key->value[i] *
					basis[0] +
				(long double)(float)end_key->value[i] *
					basis[1];
			wide += (long double)outgoing * basis[2];
			wide += (long double)incoming * basis[3];

			if (!(wide >= -SAMPLE_LIMIT && wide <= SAMPLE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"route curve sample exceeds bounds");

			result[i] = (float)wide;
		}
	}

	memcpy(output, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_route_curve_direction(const struct mh_route *route, double frame,
	float output[3], struct mh_error *error_out)
{
	float before_sample[9] = {0}, after_sample[9] = {0}, result[3];
	float time, step, distance_squared;
	long double length, inverse;
	unsigned int attempt, i;
	enum mh_error_code status;

	if (output == NULL || !(frame >= -FRAME_LIMIT && frame <= FRAME_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid route direction");

	time = (float)frame;

	for (attempt = 1; attempt < DIRECTION_ATTEMPTS; attempt++) {
		step = (float)(attempt * DIRECTION_HALF_STEP);
		status = mh_route_curve_sample(route,
			(float)((long double)time - step), before_sample,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_route_curve_sample(route,
			(float)((long double)time + step), after_sample,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 3; i++)
			result[i] = (float)((long double)after_sample[i] -
				before_sample[i]);

		distance_squared =
			(float)(((long double)result[1] * result[1] +
					(long double)result[0] * result[0]) +
				(long double)result[2] * result[2]);

		if (distance_squared >= DIRECTION_MIN_SQUARED) {
			/*
			 * Refine the length in long double before normalizing.
			 */
			length = sqrt(distance_squared);
			length = (length + distance_squared / length) * .5L;
			inverse = 1 / length;

			for (i = 0; i < 3; i++)
				result[i] = (float)(result[i] * inverse);

			memcpy(output, result, sizeof(result));

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_INVALID,
		"route has no usable direction within 99 samples");
}

enum mh_error_code
mh_route_finish_plane(const struct mh_route *route, float output[4],
	struct mh_error *error_out)
{
	float point[9], result[4], product[3];
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing finish plane output");

	status = mh_route_curve_sample(route, 0, point, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_route_curve_direction(route, 0, result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Reset stores each product before the negated x/y/z sum. */
	for (i = 0; i < 3; i++)
		product[i] = (float)((long double)point[i] * result[i]);

	result[3] =
		(float)((-(long double)product[0] - product[1]) - product[2]);
	memcpy(output, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
