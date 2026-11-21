#include <float.h>
#include <stddef.h>

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum mh_error_code
mh_clock_word(long double milliseconds, unsigned long *output,
	struct mh_error *error_out)
{
	unsigned long high;

	if (output == NULL ||
		!(milliseconds >= 0 && milliseconds < MH_CLOCK_MS_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid clock word conversion");

	high = (unsigned long)(milliseconds / MH_U32_MODULUS);
	*output = (unsigned long)(milliseconds -
		(long double)high * MH_U32_MODULUS);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
counter_valid(const struct mh_clock_counter *counter)
{
	return counter != NULL && counter->low <= MH_U32_MAX &&
		counter->high <= MH_U32_MAX;
}

enum mh_error_code
mh_clock_counter_elapsed(const struct mh_clock_counter *previous,
	const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, float speed,
	long double *elapsed, struct mh_error *error_out)
{
	unsigned long low, high;
	long double ticks, rate;

	if (elapsed == NULL || !counter_valid(previous) ||
		!counter_valid(current) || !counter_valid(frequency) ||
		frequency->high >= MH_U32_SIGN_BIT ||
		(frequency->high == 0 && frequency->low == 0) ||
		!(speed >= -FLT_MAX && speed <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid performance counter sample");

	low = (current->low - previous->low) & MH_U32_MAX;
	high = (current->high - previous->high -
		(current->low < previous->low)) &
		MH_U32_MAX;

	/*
	 * Sign the high word before combining: forming an unsigned 64-bit
	 * value and then subtracting 2^64 would lose negative low bits.
	 */
	ticks = ((long double)high -
			(high >= MH_U32_SIGN_BIT ? MH_U32_MODULUS : 0)) *
			MH_U32_MODULUS +
		low;
	rate = (long double)frequency->high * MH_U32_MODULUS + frequency->low;
	*elapsed = ticks / rate * (long double)speed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_clock_counter_ms(const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, long double *milliseconds,
	struct mh_error *error_out)
{
	struct mh_clock_counter zero;
	long double seconds = 0;
	enum mh_error_code status;

	if (milliseconds == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing real clock output");

	zero.low = zero.high = 0;
	status = mh_clock_counter_elapsed(
		&zero, current, frequency, 1, &seconds, error_out);

	if (status == MH_ERROR_NONE)
		*milliseconds = seconds * 1000.0L;

	return status;
}

enum mh_error_code
mh_clock_accumulator_step(struct mh_clock_accumulator *clock,
	const struct mh_clock_counter *current, float speed,
	struct mh_error *error_out)
{
	struct mh_clock_accumulator next;
	long double elapsed = 0, seconds;
	enum mh_error_code status;

	if (clock == NULL ||
		!(clock->seconds >= -DBL_MAX && clock->seconds <= DBL_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid accumulated clock");

	next = *clock;
	status = mh_clock_counter_elapsed(&next.counter, current,
		&next.frequency, speed, &elapsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	seconds = (long double)next.seconds + elapsed;

	if (!(seconds >= -DBL_MAX && seconds <= DBL_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"accumulated clock overflow");

	next.seconds = (double)seconds;
	next.counter = *current;
	*clock = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_clock_accumulator_reset(struct mh_clock_accumulator *clock,
	const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, struct mh_error *error_out)
{
	struct mh_clock_accumulator next;
	enum mh_error_code status;

	if (clock == NULL || frequency == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing clock initialization");

	next = *clock;
	next.seconds = 0;
	next.frequency = *frequency;
	status = mh_clock_accumulator_step(&next, current, 1, error_out);

	if (status == MH_ERROR_NONE)
		*clock = next;

	return status;
}
