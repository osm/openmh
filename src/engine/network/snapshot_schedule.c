#include <math.h>
#include <stddef.h>

#include "engine/core/limits.h"
#include "engine/network/history.h"
#include "engine/network/snapshot_schedule.h"

#define UPDATE_RATE_MAX 255
#define MILLISECONDS_PER_SECOND 1000
#define WORLD_LIMIT 1000000
/* Rounded binary64 value of 10/13. The expression 1.0/1.3 rounds lower. */
#define DETAIL_DISTANCE_SCALE .7692307692307693

int
mh_net_snapshot_stale(enum mh_net_wire wire, double received_ms, double now_ms)
{
	unsigned long deadline;

	if (wire == MH_NET_WIRE_F12)
		return now_ms - received_ms > MH_NET_STALE_MS;

	/* Compare the wrapped unsigned deadline strictly. */
	deadline = ((unsigned long)received_ms + MH_NET_PATCHED_GHOSTOUT_MS) &
		MH_U32_MAX;

	return now_ms > (double)deadline;
}

int
mh_net_snapshot_due(
	unsigned long last_sent_ms, unsigned int update_rate, double now_ms)
{
	unsigned long deadline;

	if (last_sent_ms > MH_U32_MAX || update_rate == 0 ||
		update_rate > UPDATE_RATE_MAX ||
		!(now_ms >= 0 && now_ms <= (double)MH_U32_MAX))
		return -1;

	deadline = (last_sent_ms + MILLISECONDS_PER_SECOND / update_rate) &
		MH_U32_MAX;

	return now_ms > (double)deadline;
}

int
mh_net_snapshot_detailed(
	const double source[3], const double viewed[3], double view_distance)
{
	double delta[3], detail_distance;
	long double square, distance;
	unsigned int i;

	if (source == NULL || viewed == NULL ||
		!(view_distance > 0 && view_distance <= WORLD_LIMIT) ||
		(float)view_distance == 0)
		return -1;

	for (i = 0; i < 3; i++) {
		if (!(source[i] >= -WORLD_LIMIT && source[i] <= WORLD_LIMIT) ||
			!(viewed[i] >= -WORLD_LIMIT &&
				viewed[i] <= WORLD_LIMIT))
			return -1;

		delta[i] = (double)(float)((long double)(float)viewed[i] -
			(float)source[i]);
	}

	square = (long double)delta[1] * delta[1] +
		(long double)delta[0] * delta[0] +
		(long double)delta[2] * delta[2];
	distance = sqrt((double)square);

	/*
	 * The double square root can round onto the detail threshold.
	 * Refine it in long double before the strict comparison.
	 */
	if (distance > 0)
		distance = (distance + square / distance) * .5L;

	detail_distance = (double)((long double)(float)view_distance *
		(long double)DETAIL_DISTANCE_SCALE);

	return distance < detail_distance;
}
