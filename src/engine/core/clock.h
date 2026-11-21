#ifndef MH_CORE_CLOCK_H
#define MH_CORE_CLOCK_H

#include "engine/core/error.h"
/* Exclusive upper bound for the signed 64-bit millisecond conversion. */
#define MH_CLOCK_MS_LIMIT 9223372036854775808.0L

/*
 * Low/high unsigned 32-bit words of an original performance counter.
 * Explicit words keep OS and non-C89 integer types outside the core.
 */
struct mh_clock_counter {
	unsigned long	low, high;
};

/*
 * Process counter storage. Initialization clears seconds, but retains the
 * previous counter before its first update.
 */
struct mh_clock_accumulator {
	double			seconds;
	struct mh_clock_counter	counter, frequency;
};

/*
 * Low word of the truncated signed 64-bit clock result. Callers accept
 * nonnegative finite milliseconds below 2^63. No C99 integer type or host
 * unsigned-long width is assumed. Failure preserves output.
 */
enum mh_error_code	mh_clock_word(long double milliseconds,
	unsigned long *output, struct mh_error *error_out);

/*
 * Subtract counters modulo 2^64, interpret the result as signed, divide by the
 * positive signed frequency, then multiply by binary32 speed. Keep the extended
 * result until adding to the stored binary64 game clock. A zero speed still
 * requires the platform to retain the new counter anchor. Failure leaves
 * elapsed untouched.
 */
enum mh_error_code	mh_clock_counter_elapsed(
	const struct mh_clock_counter *previous,
	const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, float speed,
	long double *elapsed, struct mh_error *error_out);

/*
 * Absolute signed counter/frequency *1000, in extended precision. Does not
 * change the accumulated game clock or its counter anchor.
 */
enum mh_error_code	mh_clock_counter_ms(
	const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, long double *milliseconds,
	struct mh_error *error_out);

enum mh_error_code	mh_clock_accumulator_step(
	struct mh_clock_accumulator *clock,
	const struct mh_clock_counter *current, float speed,
	struct mh_error *error_out);
enum mh_error_code	mh_clock_accumulator_reset(
	struct mh_clock_accumulator *clock,
	const struct mh_clock_counter *current,
	const struct mh_clock_counter *frequency, struct mh_error *error_out);

#endif
