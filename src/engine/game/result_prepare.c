#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/game/mode.h"
#include "engine/game/result_prepare.h"

#define ABORT_DRAW_LIMIT 65536UL
#define ABORT_LAP_BASE_MS 40000UL
#define ABORT_LAP_RANGE_MS 10000UL
#define ABORT_BEST_BIAS_MS 500UL
#define ABORT_BEST_RANGE_MS 1000UL
#define ABORT_GAP_BASE_MS 100UL
#define ABORT_GAP_RANGE_MS 3000UL
#define FINISH_JITTER_RANGE_MS 2000UL

enum mh_error_code
mh_result_abort_positions(struct mh_result_estimate *cars, unsigned int count,
	unsigned int local_entry, unsigned int mode, int aborted,
	unsigned long *random, struct mh_error *error_out)
{
	struct mh_result_estimate next[MH_STANDINGS_MAX_CARS];
	unsigned int active_count, i, j, position;
	unsigned long seed, attempts;

	if (cars == NULL || random == NULL || count < 1 ||
		count > MH_STANDINGS_MAX_CARS || local_entry >= count ||
		*random > MH_U32_MAX || (aborted != 0 && aborted != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid aborted results context");

	if (mode == MH_GAME_NETWORK || !aborted)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	active_count = 0;

	for (i = 0; i < count; i++) {
		if (cars[i].active != 0 && cars[i].active != 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid aborted results row");

		if (cars[i].active)
			active_count++;
	}

	memcpy(next, cars, count * sizeof(*cars));
	seed = *random;
	next[local_entry].position = active_count;

	for (i = 1; i < active_count; i++) {
		attempts = 0;

		do {
			if (++attempts > ABORT_DRAW_LIMIT)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"aborted results random selection exhausted");

			position = (unsigned int)(mh_random_next(&seed) %
					active_count) +
				1;

			for (j = 0; j < i && next[j].position != position;
				j++) {
			}
		} while (j != i);

		next[i].position = position;
	}

	memcpy(cars, next, count * sizeof(*cars));
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_result_abort_times(struct mh_result_estimate *cars, unsigned int count,
	unsigned int local_entry, unsigned int mode, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_result_estimate next[MH_STANDINGS_MAX_CARS];
	unsigned int i, rank;
	unsigned long seed, total, best;
	enum mh_error_code status;

	if (cars == NULL || random == NULL || count < 1 ||
		count > MH_STANDINGS_MAX_CARS || local_entry >= count ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid aborted results context");

	if (mode == MH_GAME_NETWORK)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < count; i++)
		if ((cars[i].finished != 0 && cars[i].finished != 1) ||
			cars[i].lap_limit == 0 ||
			cars[i].lap_limit > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid aborted results lap limit");

	memcpy(next, cars, count * sizeof(*cars));
	seed = *random;
	status = mh_result_abort_positions(
		next, count, local_entry, mode, 1, &seed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Use row zero's lap limit even for a different local row. */
	total = ((ABORT_LAP_BASE_MS +
			mh_random_next(&seed) % ABORT_LAP_RANGE_MS) *
			next[0].lap_limit) &
		MH_U32_MAX;
	next[local_entry].total_ms = total;
	next[local_entry].best_ms = 0;

	for (rank = 1; rank <= count; rank++)
		for (i = 0; i < count; i++) {
			if (i == local_entry || !next[i].active ||
				next[i].position != rank)
				continue;

			next[i].total_ms = total;
			best = total;

			if (next[i].lap_limit != 1)
				best = (total / next[i].lap_limit -
					ABORT_BEST_BIAS_MS -
					mh_random_next(&seed) %
						ABORT_BEST_RANGE_MS) &
					MH_U32_MAX;

			next[i].best_ms = best;
			total = (total + ABORT_GAP_BASE_MS +
					mh_random_next(&seed) %
						ABORT_GAP_RANGE_MS) &
				MH_U32_MAX;
		}

	memcpy(cars, next, count * sizeof(*cars));
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Truncate to a signed 64-bit temporary, then keep its low word. */
static unsigned long
estimate_time_word(long double value)
{
	unsigned long word;
	int negative;

	negative = value < 0;

	if (negative)
		value = -value;

	while (value >= MH_U32_MODULUS)
		value -= MH_U32_MODULUS;

	word = (unsigned long)value;

	return negative ? (0UL - word) & MH_U32_MAX : word;
}

static unsigned long
estimate_finish_time(const struct mh_result_estimate *car,
	unsigned long base_ms, unsigned long best_ms, float route_duration,
	unsigned long *seed)
{
	unsigned long remaining_laps, difference_ms, jitter_ms;
	long signed_best_ms;
	float adjustment_ms;
	long double projected_ms;

	remaining_laps = (car->lap_limit - car->lap + 1) & MH_U32_MAX;
	difference_ms = (car->total_ms - base_ms + best_ms * remaining_laps) &
		MH_U32_MAX;
	signed_best_ms = best_ms <= (unsigned long)MH_I32_MAX
		? (long)best_ms
		: -1L - (long)(MH_U32_MAX - best_ms);

	/* Round only the final time adjustment to float. */
	projected_ms = (long double)signed_best_ms * car->route_parameter;
	projected_ms /= route_duration;
	adjustment_ms = (float)((long double)difference_ms - projected_ms);
	jitter_ms = mh_random_next(seed) % FINISH_JITTER_RANGE_MS;

	return estimate_time_word((long double)car->total_ms +
		((long double)jitter_ms + adjustment_ms));
}

enum mh_error_code
mh_result_estimate_times(struct mh_result_estimate *cars, unsigned int count,
	unsigned int mode, float route_duration, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_result_estimate next[MH_STANDINGS_MAX_CARS];
	unsigned int i, rank, base_entry;
	unsigned long seed, best_ms;
	int have_base;

	if (cars == NULL || random == NULL || count < 1 ||
		count > MH_STANDINGS_MAX_CARS || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid result estimate context");

	if (mode == MH_GAME_NETWORK)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!(route_duration > 0 && route_duration <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid result route duration");

	for (i = 0; i < count; i++)
		if ((cars[i].active != 0 && cars[i].active != 1) ||
			(cars[i].finished != 0 && cars[i].finished != 1) ||
			cars[i].lap > MH_U32_MAX || cars[i].lap_limit == 0 ||
			cars[i].lap_limit > MH_U32_MAX ||
			cars[i].total_ms > MH_U32_MAX ||
			cars[i].best_ms > MH_U32_MAX ||
			!(cars[i].route_parameter >= 0 &&
				cars[i].route_parameter <= route_duration))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid result estimate row");

	memcpy(next, cars, count * sizeof(*cars));
	seed = *random;
	base_entry = 0;
	best_ms = 0;
	have_base = 0;

	for (rank = 1; rank <= count; rank++)
		for (i = 0; i < count; i++) {
			if (!next[i].active || next[i].position != rank)
				continue;

			if (next[i].finished || rank == 1) {
				base_entry = i;
				best_ms = next[i].best_ms;
				have_base = 1;
				continue;
			}

			if (!have_base)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"missing result estimate leader");

			next[i].total_ms = estimate_finish_time(&next[i],
				next[base_entry].total_ms, best_ms,
				route_duration, &seed);

			/* Leave multi-lap best times alone. */
			if (next[i].lap_limit == 1)
				next[i].best_ms = next[i].total_ms;

			best_ms = next[i].best_ms;
		}

	memcpy(cars, next, count * sizeof(*cars));
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
