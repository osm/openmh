#include <stdio.h>
#include <string.h>

#include "engine/clock/paused.h"
#include "engine/core/limits.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "paused clock:%d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_fixture {
	struct mh_clock_counter	counter;
	unsigned long		ticks;
};

static struct mh_clock_counter
counter(void *context)
{
	return ((struct clock_fixture *)context)->counter;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter rate;

	(void)context;
	rate.low = 1000;
	rate.high = 0;

	return rate;
}

static unsigned long
ticks(void *context)
{
	return ((struct clock_fixture *)context)->ticks;
}

int
main(void)
{
	static const struct mh_clock_ops ops = {
		counter, frequency, ticks, NULL, NULL};

	struct clock_fixture fixture;
	struct mh_clock_source original;
	struct mh_paused_clock clock;
	struct mh_clock_counter now;
	struct mh_clock_date date;

	memset(&fixture, 0, sizeof(fixture));
	fixture.counter.low = fixture.ticks = 100;
	CHECK(mh_clock_source_init(&original, &ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_paused_clock_init(&clock, &original, NULL) == MH_ERROR_NONE);
	CHECK(mh_clock_source_ms(&clock.source) == 100);
	CHECK(!mh_clock_source_can_delay(&clock.source));
	CHECK(mh_clock_source_date(&clock.source, &date, NULL) ==
		MH_ERROR_UNSUPPORTED);
	mh_paused_clock_set(&clock, 1);
	fixture.counter.low = fixture.ticks = 60100;
	mh_paused_clock_set(&clock, 1);
	CHECK(mh_clock_source_ms(&clock.source) == 100);
	CHECK(mh_clock_source_ticks(&clock.source) == 100);
	mh_paused_clock_set(&clock, 0);
	CHECK(mh_clock_source_ms(&clock.source) == 100);
	fixture.counter.low += 16;
	fixture.ticks += 16;
	mh_paused_clock_set(&clock, 0);
	CHECK(mh_clock_source_ms(&clock.source) == 116);
	CHECK(mh_clock_source_ticks(&clock.source) == 116);

	/*
	 * Repeated suspension spanning the low counter and tick word rollover.
	 */
	fixture.counter.low = fixture.ticks = MH_U32_MAX - 4;
	mh_paused_clock_set(&clock, 1);
	fixture.counter.low = fixture.ticks = 5;
	fixture.counter.high = 1;
	mh_paused_clock_set(&clock, 0);
	now = mh_clock_source_counter(&clock.source);
	CHECK(now.high == 0 && now.low == MH_U32_MAX - 60004);
	CHECK(mh_clock_source_ticks(&clock.source) == now.low);
	fixture.counter.low += 7;
	fixture.ticks += 7;
	now = mh_clock_source_counter(&clock.source);
	CHECK(now.high == 0 && now.low == MH_U32_MAX - 59997);
	puts("Pausable clock preserves active time, repeated pauses and counter rollover");

	return 0;
}
