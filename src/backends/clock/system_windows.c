#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "backends/clock/system_platform.h"
#include "engine/core/limits.h"

static struct mh_clock_counter
counter_words(const LARGE_INTEGER *value)
{
	struct mh_clock_counter result;

	result.low = value->LowPart;
	result.high = (unsigned long)value->HighPart;

	return result;
}

struct mh_clock_counter
mh_system_clock_frequency(void *context)
{
	LARGE_INTEGER value;

	(void)context;
	value.LowPart = 0;
	value.HighPart = 0;
	QueryPerformanceFrequency(&value);

	return counter_words(&value);
}

struct mh_clock_counter
mh_system_clock_counter(void *context)
{
	static LARGE_INTEGER last;
	LARGE_INTEGER value;

	(void)context;

	/* Match the POSIX provider: a failed sample holds time still. */
	if (QueryPerformanceCounter(&value))
		last = value;

	return counter_words(&last);
}

unsigned long
mh_system_clock_ticks(void *context)
{
	static struct mh_clock_counter origin;
	static int started;
	struct mh_clock_counter now, rate;
	long double elapsed;
	unsigned long milliseconds;

	now = mh_system_clock_counter(context);
	rate = mh_system_clock_frequency(context);

	if (!started) {
		origin = now;
		started = 1;
	}

	milliseconds = 0;

	if (mh_clock_counter_elapsed(&origin, &now, &rate, 1, &elapsed, NULL) ==
		MH_ERROR_NONE)
		mh_clock_word(elapsed * MH_SYSTEM_CLOCK_MS_PER_SECOND,
			&milliseconds, NULL);

	return milliseconds;
}

void
mh_system_clock_delay(void *context, unsigned long milliseconds)
{
	struct mh_clock_counter before, after, rate;
	long double remaining, elapsed;
	unsigned long delay;

	milliseconds &= MH_U32_MAX;
	remaining = milliseconds;
	before = mh_system_clock_counter(context);
	rate = mh_system_clock_frequency(context);

	do {
		/* Keep each wait finite: DWORD_MAX means INFINITE to Sleep. */
		delay = remaining > MH_I32_MAX ? MH_I32_MAX
					: (unsigned long)remaining;

		if (delay < remaining && delay < MH_I32_MAX)
			delay++;

		Sleep(delay);
		after = mh_system_clock_counter(context);

		if (mh_clock_counter_elapsed(&before, &after, &rate, 1,
			&elapsed, NULL) != MH_ERROR_NONE ||
			elapsed <= 0)
			return;

		/*
		 * Sleep can return early because of the system timer
		 * resolution.
		 */
		remaining =
			milliseconds - elapsed * MH_SYSTEM_CLOCK_MS_PER_SECOND;
	} while (remaining > 0);
}
