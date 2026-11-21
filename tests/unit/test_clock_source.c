#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "clock source line %d: %s\n",          \
				__LINE__, #c);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_fixture {
	struct mh_clock_date	date;
	int			fail_date;
	struct mh_clock_counter	counter, frequency;
	unsigned long		ticks, delayed;
	unsigned int		samples, waits;
};

static struct mh_clock_counter
counter(void *context)
{
	struct clock_fixture *fixture;

	fixture = context;
	fixture->samples++;

	return fixture->counter;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct clock_fixture *fixture;

	fixture = context;

	return fixture->frequency;
}

static unsigned long
ticks(void *context)
{
	struct clock_fixture *fixture;

	fixture = context;

	return fixture->ticks;
}

static void
delay(void *context, unsigned long milliseconds)
{
	struct clock_fixture *fixture;

	fixture = context;
	fixture->delayed = milliseconds;
	fixture->waits++;
}

static enum mh_error_code
calendar(
	void *context, struct mh_clock_date *output, struct mh_error *error_out)
{
	struct clock_fixture *fixture;

	fixture = context;
	*output = fixture->date;

	return fixture->fail_date
		? mh_error_set(
			error_out, MH_ERROR_IO, "injected calendar failure")
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	static const struct mh_clock_ops native_ops = {
		counter, frequency, ticks, delay, calendar};

	static const struct mh_clock_ops host_ops = {
		counter, frequency, ticks, NULL, NULL};

	struct mh_clock_ops incomplete;
	struct mh_clock_source first, second;
	struct clock_fixture native, host;
	struct mh_clock_counter before, after, rate;
	struct mh_clock_date date;
	struct mh_error error;
	long double elapsed;
	unsigned long word;

	memset(&native, 0, sizeof(native));
	memset(&host, 0, sizeof(host));
	native.date.year = 1998;
	native.date.month = 5;
	native.date.day = 20;
	native.date.hour = 23;
	native.date.minute = 59;
	native.counter.high = 3;
	native.counter.low = 1000;
	native.frequency.low = 1000;
	native.ticks = MH_U32_MAX;
	host.counter.high = 7;
	host.counter.low = 500;
	host.frequency.low = 2000;
	host.ticks = 42;
	CHECK(mh_clock_source_init(&first, &native_ops, &native, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&second, &host_ops, &host, &error) ==
		MH_ERROR_NONE);

	before = mh_clock_source_counter(&first);
	native.counter.low += 250;
	after = mh_clock_source_counter(&first);
	rate = mh_clock_source_frequency(&first);
	CHECK(before.high == 3 && after.high == 3 && before.low == 1000 &&
		after.low == 1250);
	CHECK(mh_clock_counter_elapsed(&before, &after, &rate, 1, &elapsed,
		&error) == MH_ERROR_NONE);
	CHECK(elapsed == .25L && native.samples == 2 && host.samples == 0);
	CHECK(mh_clock_source_counter(&second).high == 7);
	CHECK(mh_clock_source_frequency(&second).low == 2000);
	CHECK(host.samples == 1 && native.samples == 2);
	CHECK(mh_clock_source_ticks(&first) == MH_U32_MAX);
	CHECK(mh_clock_source_ticks(&second) == 42);

	CHECK(mh_clock_source_can_delay(&first));
	CHECK(!mh_clock_source_can_delay(&second));
	CHECK(mh_clock_source_delay(&first, 17, &error) == MH_ERROR_NONE);
	CHECK(native.delayed == 17 && native.waits == 1 && host.waits == 0);
	CHECK(mh_clock_source_delay(&second, 17, &error) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(error.code == MH_ERROR_UNSUPPORTED && host.waits == 0);
	CHECK(native.samples == 2 && host.samples == 1);

	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_NONE);
	CHECK(date.year == 1998 && date.month == 5 && date.day == 20 &&
		date.hour == 23 && date.minute == 59);
	CHECK(mh_clock_source_date(&second, &date, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(date.year == 1998 && date.month == 5 && date.day == 20 &&
		date.hour == 23 && date.minute == 59);
	native.date.hour = 24;
	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_INVALID &&
		date.hour == 23);
	native.date.hour = 23;
	native.date.minute = 60;
	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_INVALID &&
		date.minute == 59);
	native.date.minute = 59;
	native.date.day = 21;
	native.fail_date = 1;
	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_IO &&
		date.day == 20);
	native.fail_date = 0;
	native.date.month = 13;
	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_INVALID &&
		date.month == 5);
	native.date.month = 5;
	CHECK(mh_clock_source_date(&first, &date, NULL) == MH_ERROR_NONE &&
		date.day == 21);
	CHECK(native.samples == 2 && host.samples == 1 && native.waits == 1 &&
		host.waits == 0);

	/*
	 * A rejected descriptor cannot replace an already initialized source.
	 */
	incomplete = native_ops;
	incomplete.counter = NULL;
	CHECK(mh_clock_source_init(&first, &incomplete, &host, &error) ==
		MH_ERROR_INVALID);
	CHECK(first.ops == &native_ops && first.context == &native);
	incomplete = native_ops;
	incomplete.frequency = NULL;
	CHECK(mh_clock_source_init(&first, &incomplete, &host, NULL) ==
		MH_ERROR_INVALID);
	incomplete = native_ops;
	incomplete.ticks = NULL;
	CHECK(mh_clock_source_init(&first, &incomplete, &host, NULL) ==
		MH_ERROR_INVALID);
	CHECK(first.ops == &native_ops && first.context == &native);
	CHECK(mh_clock_source_delay(&first, 0, &error) == MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(native.waits == 2 && host.waits == 0);

	CHECK(mh_clock_source_ms(&first) == 3 * MH_U32_MODULUS + 1250);
	CHECK(mh_clock_source_word(&first, &word, NULL) == MH_ERROR_NONE);
	CHECK(word == 1250);
	CHECK(mh_clock_source_ms(&second) == 7 * MH_U32_MODULUS / 2 + 250);
	native.frequency.low = 0;
	word = 123;
	CHECK(mh_clock_source_ms(&first) == -1);
	CHECK(mh_clock_source_word(&first, &word, NULL) != MH_ERROR_NONE);
	CHECK(word == 123);

	puts("Clock providers preserve counters, isolate contexts and expose optional pacing");

	return 0;
}
