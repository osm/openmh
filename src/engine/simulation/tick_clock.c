#include <float.h>
#include <stddef.h>

#include "engine/core/limits.h"
#include "engine/simulation/tick_clock.h"

static long double
boundary(const struct mh_tick_clock *clock)
{
	/* Only compensate floating arithmetic at a mathematical boundary. */
	return 16 * LDBL_EPSILON * (clock->total + 1);
}

enum mh_error_code
mh_tick_clock_init(struct mh_tick_clock *clock, unsigned int rate,
	struct mh_error *error_out)
{
	if (clock == NULL || !mh_sim_rate_valid(rate))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"simulation rate must be an integer from 1 to 4294967295 Hz");

	clock->rate = rate;
	clock->ticks = 0;
	clock->total = clock->compensation = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_tick_clock_add(struct mh_tick_clock *clock, long double elapsed,
	int suspended, struct mh_error *error_out)
{
	long double increment, total;

	if (clock == NULL || !mh_sim_rate_valid(clock->rate) ||
		!(elapsed >= 0 && elapsed <= 1000000000L) ||
		(suspended != 0 && suspended != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid simulation clock interval");

	if (suspended)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Kahan summation retains sub-tick fractions over long sessions. */
	increment = elapsed * clock->rate - clock->compensation;
	total = clock->total + increment;

	if (!(total >= 0 && total <= MH_U32_MAX - 2))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"simulation clock exhausted");

	clock->compensation = (total - clock->total) - increment;
	clock->total = total;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_tick_clock_due(const struct mh_tick_clock *clock)
{
	long double due;

	due = clock->total - clock->ticks + boundary(clock);

	if (due < 1)
		return 0;

	if (due >= MH_SIM_CATCHUP_LIMIT)
		return MH_SIM_CATCHUP_LIMIT;

	return (unsigned int)due;
}

enum mh_error_code
mh_tick_clock_commit(struct mh_tick_clock *clock, struct mh_error *error_out)
{
	if (clock == NULL || !mh_sim_rate_valid(clock->rate) ||
		mh_tick_clock_due(clock) == 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"no complete simulation tick available");

	clock->ticks++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

double
mh_tick_clock_alpha(const struct mh_tick_clock *clock)
{
	long double remainder;

	remainder = clock->total - clock->ticks;

	if (remainder <= boundary(clock))
		return 0;

	if (remainder >= 1)
		return 1;

	return (double)remainder;
}

unsigned long
mh_tick_clock_input_tick(const struct mh_tick_clock *clock)
{
	unsigned long whole;

	whole = (unsigned long)clock->total;

	if (clock->total - whole > boundary(clock))
		whole++;

	return whole + 1;
}
