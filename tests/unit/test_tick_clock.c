#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/simulation/tick_clock.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "tick clock line %d: %s\n", __LINE__,  \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
schedule(unsigned int rate, unsigned int fps)
{
	struct mh_tick_clock clock;
	unsigned long frame;
	unsigned int due, i;

	CHECK(mh_tick_clock_init(&clock, rate, NULL) == MH_ERROR_NONE);

	/* Ten minutes, including non-divisors of the simulation rate. */
	for (frame = 0; frame < 600UL * fps; frame++) {
		CHECK(mh_tick_clock_add(&clock, 1.0L / fps, 0, NULL) ==
			MH_ERROR_NONE);
		due = mh_tick_clock_due(&clock);

		for (i = 0; i < due; i++)
			CHECK(mh_tick_clock_commit(&clock, NULL) ==
				MH_ERROR_NONE);

		CHECK(clock.ticks == (frame + 1) * rate / fps);
		CHECK(mh_tick_clock_alpha(&clock) >= 0 &&
			mh_tick_clock_alpha(&clock) < 1);
	}

	CHECK(clock.ticks == 600UL * rate);
	CHECK(mh_tick_clock_alpha(&clock) == 0);

	return 0;
}

static int
jitter(void)
{
	struct mh_tick_clock clock;
	unsigned long milliseconds, interval, i;
	unsigned int due, j;

	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);
	milliseconds = 0;

	for (i = 0; i < 100000; i++) {
		/*
		 * Alternating short frames and stalls, with retained overload.
		 */
		interval = i % 43 == 0 ? 400 : 1 + (i * 17) % 23;
		milliseconds += interval;
		CHECK(mh_tick_clock_add(&clock, interval / 1000.0L, 0, NULL) ==
			MH_ERROR_NONE);
		due = mh_tick_clock_due(&clock);
		CHECK(due <= MH_SIM_CATCHUP_LIMIT);

		for (j = 0; j < due; j++)
			CHECK(mh_tick_clock_commit(&clock, NULL) ==
				MH_ERROR_NONE);
	}

	while (mh_tick_clock_due(&clock))
		CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);

	CHECK(clock.ticks == milliseconds * 120 / 1000);
	CHECK(fabs(mh_tick_clock_alpha(&clock) -
		(milliseconds * 120 % 1000) / 1000.0) < 1e-9);

	return 0;
}

static int
render_schedules(void)
{
	const unsigned int fps[] = {30, 60, 120, 144, 200, 260, 360};
	const unsigned int rates[] = {60, 120, 240, 100, 144};
	unsigned int i, j;

	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		for (j = 0; j < sizeof(fps) / sizeof(fps[0]); j++) {
			if (schedule(rates[i], fps[j]) != 0) {
				fprintf(stderr,
					"schedule: simulation=%u Hz, render=%u FPS\n",
					rates[i], fps[j]);
				return 1;
			}
		}
	}

	return 0;
}

static int
rate_boundaries(void)
{
	struct mh_tick_clock clock;

	CHECK(mh_tick_clock_init(&clock, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_due(&clock) == 1);
	CHECK(mh_tick_clock_init(&clock, MH_SIM_RATE_MAX, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, 1.0L / MH_SIM_RATE_MAX, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_tick_clock_due(&clock) == 1);

	return 0;
}

static int
input_tick_boundary(void)
{
	struct mh_tick_clock clock;

	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_input_tick(&clock) == 1);
	CHECK(mh_tick_clock_add(&clock, 1.0L / 120 - 1e-9L, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_tick_clock_due(&clock) == 0);
	CHECK(mh_tick_clock_input_tick(&clock) == 2);
	CHECK(mh_tick_clock_add(&clock, 1e-9L, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_due(&clock) == 1);
	CHECK(mh_tick_clock_input_tick(&clock) == 2);
	CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_STATE);

	return 0;
}

static int
pause_preserves_clock(void)
{
	struct mh_tick_clock clock, saved;

	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, 1.0L / 120, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);
	saved = clock;
	CHECK(mh_tick_clock_add(&clock, 3600, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&clock, &saved, sizeof(clock)) == 0);

	return 0;
}

static int
errors_preserve_clock(void)
{
	struct mh_tick_clock clock, saved;

	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, 1.0L / 120, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);
	saved = clock;
	CHECK(mh_tick_clock_add(&clock, -1, 0, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&clock, &saved, sizeof(clock)) == 0);
	CHECK(mh_tick_clock_add(&clock, 1000000000L, 0, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&clock, &saved, sizeof(clock)) == 0);
	CHECK(mh_tick_clock_init(&clock, 0, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&clock, &saved, sizeof(clock)) == 0);

	return 0;
}

static int
retained_backlog(void)
{
	struct mh_tick_clock clock;

	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, 1.0L / 120, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_add(&clock, .5L, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_clock_due(&clock) == MH_SIM_CATCHUP_LIMIT);
	CHECK(mh_tick_clock_alpha(&clock) == 1);
	CHECK(mh_tick_clock_input_tick(&clock) == 62);

	while (mh_tick_clock_due(&clock))
		CHECK(mh_tick_clock_commit(&clock, NULL) == MH_ERROR_NONE);

	CHECK(clock.ticks == 61);

	return 0;
}

static int
long_running_precision(void)
{
	struct mh_tick_clock clock;
	unsigned long i;

	/* A long-running clock retains the fraction across many additions. */
	CHECK(mh_tick_clock_init(&clock, 120, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 1000000; i++) {
		CHECK(mh_tick_clock_add(&clock, 1.0L / 144, 0, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(clock.total >= 833333.333333332L &&
		clock.total <= 833333.333333334L);

	return 0;
}

int
main(void)
{
	CHECK(render_schedules() == 0);
	CHECK(jitter() == 0);
	CHECK(rate_boundaries() == 0);
	CHECK(input_tick_boundary() == 0);
	CHECK(pause_preserves_clock() == 0);
	CHECK(errors_preserve_clock() == 0);
	CHECK(retained_backlog() == 0);
	CHECK(long_running_precision() == 0);
	puts("fixed tick counts, fractions, pause, jitter and retained backlog passed");

	return 0;
}
