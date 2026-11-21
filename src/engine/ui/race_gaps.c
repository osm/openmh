#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/race_gaps.h"

#define MISSING_CROSSING_MS 100000000UL

static unsigned long
crossing_time(const struct mh_race *viewed_race, const struct mh_race *other)
{
	unsigned long value;

	if (viewed_race->finished)
		return other->finished ? other->now_ms : MISSING_CROSSING_MS;

	value = other->splits[viewed_race->lap]
			[viewed_race->split_notice == MH_RACE_SPLIT_LAP
					? 0
					: viewed_race->split_notice];

	return value != 0 ? value : MISSING_CROSSING_MS;
}

static void
sort_crossings(
	const unsigned long *times, unsigned int *order, unsigned int count)
{
	unsigned int i, j, swap;

	for (i = 1; i < count; i++)
		for (j = 0; j < count - i; j++)
			if ((float)times[order[j]] >
				(float)times[order[j + 1]]) {
				swap = order[j];
				order[j] = order[j + 1];
				order[j + 1] = swap;
			}
}

enum mh_error_code
mh_race_gaps(const struct mh_race *const *cars, unsigned int count,
	unsigned int active_mask, unsigned int viewed, unsigned int mode,
	struct mh_race_gaps *output, struct mh_error *error_out)
{
	struct mh_race_gaps next;
	const struct mh_race *viewed_race;
	unsigned int order[MH_GAPS_MAX_CARS], i, crossing_count, viewed_rank,
		column;
	unsigned long times[MH_GAPS_MAX_CARS], viewed_time, value;

	if (cars == NULL || output == NULL || count == 0 ||
		count > MH_GAPS_MAX_CARS || viewed >= count ||
		mode > MH_GAPS_ALL || (active_mask >> count) != 0 ||
		!(active_mask & (1U << viewed)) || cars[viewed] == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race gap roster");

	viewed_race = cars[viewed];

	if (viewed_race->lap < 1 || viewed_race->lap > MH_RACE_MAX_LAPS ||
		viewed_race->split_notice >= MH_RACE_SPLIT_COUNT ||
		viewed_race->split_hold_ms > MH_RACE_SPLIT_HOLD_LIMIT_MS ||
		(viewed_race->finished != 0 && viewed_race->finished != 1) ||
		viewed_race->now_ms > (unsigned long)MH_I32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race gap reference");

	memset(&next, 0, sizeof(next));
	next.mode = viewed_race->finished ? MH_GAPS_ALL : mode;

	if (next.mode == MH_GAPS_OFF || viewed_race->split_notice == 0 ||
		viewed_race->split_hold_ms == 0) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	column = viewed_race->split_notice == MH_RACE_SPLIT_LAP
		? 0
		: viewed_race->split_notice;
	viewed_time = viewed_race->finished
		? viewed_race->now_ms
		: viewed_race->splits[viewed_race->lap][column];

	if (viewed_time > (unsigned long)MH_I32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid viewed crossing time");

	crossing_count = 0;

	for (i = 0; i < count; i++)
		if (active_mask & (1U << i)) {
			if (cars[i] == NULL ||
				(cars[i]->finished != 0 &&
					cars[i]->finished != 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid race gap car");

			value = crossing_time(viewed_race, cars[i]);

			if (value > (unsigned long)MH_I32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid race gap crossing time");

			/*
			 * Original stores signed integer time as binary32, then
			 * compares the sentinel bits. Neighboring large times
			 * can round to it too.
			 */
			if ((float)value == (float)MISSING_CROSSING_MS)
				continue;

			order[crossing_count++] = i;
			times[i] = value;
		}

	sort_crossings(times, order, crossing_count);

	/* Original caller initializes its lookup result to zero. */
	viewed_rank = 0;

	for (i = 0; i < crossing_count; i++)
		if (order[i] == viewed)
			viewed_rank = i;

	for (i = 0; i < crossing_count; i++) {
		if (next.mode == MH_GAPS_NEIGHBORS &&
			!((i == 0 && viewed_rank != 0) ||
				(i + 1 == viewed_rank && viewed_rank != 1) ||
				i == viewed_rank + 1))
			continue;

		next.rows[next.count].entry = order[i];
		next.rows[next.count].position =
			next.mode == MH_GAPS_NEIGHBORS ? i + 1 : 0;
		next.rows[next.count].milliseconds =
			(long)times[order[i]] - (long)viewed_time;
		next.count++;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
