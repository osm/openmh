#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <time.h>

#include "backends/clock/system_platform.h"
#include "engine/core/limits.h"

/* Preserve the Linux performance clock's independence from NTP adjustments. */
#if defined(__linux__) && defined(CLOCK_MONOTONIC_RAW)
#define PERFORMANCE_CLOCK CLOCK_MONOTONIC_RAW
#else
#define PERFORMANCE_CLOCK CLOCK_MONOTONIC
#endif

#define COUNTER_SCALE 1000UL
#define COUNTER_SCALE_STEPS 3

static struct timespec
sample(void)
{
	static struct timespec last;
	struct timespec now;

	/*
	 * The counter API has no error result; a failed sample holds time
	 * still.
	 */
	if (clock_gettime(PERFORMANCE_CLOCK, &now) == 0)
		last = now;

	return last;
}

static struct mh_clock_counter
counter_words(const struct timespec *time)
{
	struct mh_clock_counter result;
	unsigned long low, high;
	unsigned int i;

	result.low = (unsigned long)time->tv_sec & MH_U32_MAX;
	result.high = (unsigned long)((time->tv_sec / MH_U16_MODULUS) /
			MH_U16_MODULUS) &
		MH_U32_MAX;

	/* Multiply seconds by 1000^3 using 16-bit pieces, even on ILP32 C89. */
	for (i = 0; i < COUNTER_SCALE_STEPS; i++) {
		low = (result.low & MH_U16_MAX) * COUNTER_SCALE;
		high = (result.low >> 16) * COUNTER_SCALE + (low >> 16);
		result.high = (result.high * COUNTER_SCALE + (high >> 16)) &
			MH_U32_MAX;
		result.low = ((high & MH_U16_MAX) << 16) | (low & MH_U16_MAX);
	}

	low = (result.low + (unsigned long)time->tv_nsec) & MH_U32_MAX;
	result.high = (result.high + (low < result.low)) & MH_U32_MAX;
	result.low = low;

	return result;
}

struct mh_clock_counter
mh_system_clock_frequency(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = MH_SYSTEM_CLOCK_FREQUENCY;
	result.high = 0;

	return result;
}

struct mh_clock_counter
mh_system_clock_counter(void *context)
{
	struct timespec now;

	(void)context;
	now = sample();

	return counter_words(&now);
}

unsigned long
mh_system_clock_ticks(void *context)
{
	static struct timespec origin;
	static int started;
	struct timespec now;
	unsigned long seconds;
	long nanoseconds;

	(void)context;
	now = sample();

	if (!started) {
		origin = now;
		started = 1;
	}

	seconds = (unsigned long)(now.tv_sec - origin.tv_sec);
	nanoseconds = now.tv_nsec - origin.tv_nsec;

	if (nanoseconds < 0) {
		seconds--;
		nanoseconds += MH_SYSTEM_CLOCK_FREQUENCY;
	}

	return (seconds * MH_SYSTEM_CLOCK_MS_PER_SECOND +
		(unsigned long)nanoseconds / MH_SYSTEM_CLOCK_NS_PER_MS) &
		MH_U32_MAX;
}

void
mh_system_clock_delay(void *context, unsigned long milliseconds)
{
	struct timespec request, remaining;

	(void)context;
	milliseconds &= MH_U32_MAX;
	request.tv_sec = (time_t)(milliseconds / MH_SYSTEM_CLOCK_MS_PER_SECOND);
	request.tv_nsec =
		(long)((milliseconds % MH_SYSTEM_CLOCK_MS_PER_SECOND) *
			MH_SYSTEM_CLOCK_NS_PER_MS);

	while (nanosleep(&request, &remaining) != 0 && errno == EINTR)
		request = remaining;
}
