#ifndef MH_TEST_FRAME_COUNTER_FIXTURE_H
#define MH_TEST_FRAME_COUNTER_FIXTURE_H

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

/* Exercise clock rates, low-word wrap, zero intervals and long stalls. */
static enum mh_error_code
counter_sample(
	unsigned int frame, long double *elapsed, struct mh_error *error_out)
{
	static const unsigned long rates[4] = {
		1000000000UL, 10000000UL, 3579545UL, 1193182UL};

	static const unsigned long divisors[10] = {
		30, 60, 120, 144, 1000, 25, 25, 4, 2, 1};

	struct mh_clock_counter previous, current, frequency;
	unsigned long low, high;
	enum mh_error_code status;

	frequency.low = rates[frame % 4];
	frequency.high = 0;

	/* Begin 96 counts before the low-word wrap boundary. */
	previous.low = 4294967200UL;
	previous.high = (unsigned long)MH_I32_MAX + frame;
	low = frequency.low / divisors[frame % 10];
	high = 0;

	if (frame % 10 == 6)
		low *= 2;

	if (frame % 10 == 9)
		low = 0;

	if (frame == 200 || frame == 600) {
		low = 2820130816UL;
		high = 4;
	}

	current.low = (previous.low + low) & MH_U32_MAX;
	current.high = previous.high + high + (low > MH_U32_MAX - previous.low);
	status = mh_clock_counter_elapsed(
		&previous, &current, &frequency, 1, elapsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#endif
