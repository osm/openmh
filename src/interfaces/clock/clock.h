#ifndef MH_INTERFACES_CLOCK_CLOCK_H
#define MH_INTERFACES_CLOCK_CLOCK_H

#include "engine/core/clock.h"

struct mh_clock_source;

/* Host-local calendar, independent of monotonic counters and game time. */
struct mh_clock_date {
	unsigned int	year, month, day, hour, minute;
};

/*
 * Optional immediate calendar query. Year is absolute (at least 1900), month
 * is 1..12, day is 1..31, hour is 0..23 and minute is 0..59. No time_t, epoch
 * or timezone API is required. Missing support returns UNSUPPORTED; errors
 * preserve output. This call does not sample or change the game timer.
 * Providers may return a host-cached date.
 */
enum mh_error_code	mh_clock_source_date(
	const struct mh_clock_source *source, struct mh_clock_date *output,
	struct mh_error *error_out);

/* Sources are borrowed; the host/implementation owns their lifetime. */
struct mh_clock_counter	mh_clock_source_counter(
	const struct mh_clock_source *source);
struct mh_clock_counter	mh_clock_source_frequency(
	const struct mh_clock_source *source);
unsigned long		mh_clock_source_ticks(
	const struct mh_clock_source *source);

/* Absolute signed counter/frequency time; -1 on invalid counter words/rate. */
long double mh_clock_source_ms(const struct mh_clock_source *source);

/* Original truncation/wrap to 32 bits; failure preserves milliseconds. */
enum mh_error_code	mh_clock_source_word(
	const struct mh_clock_source *source, unsigned long *milliseconds,
	struct mh_error *error_out);
int			mh_clock_source_can_delay(
	const struct mh_clock_source *source);

/* Missing native pacing returns UNSUPPORTED; it never busy-waits instead. */
enum mh_error_code	mh_clock_source_delay(
	const struct mh_clock_source *source, unsigned long milliseconds,
	struct mh_error *error_out);

#endif
