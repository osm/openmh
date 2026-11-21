#include <stdio.h>
#include <string.h>

#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "interfaces/clock/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "platform clock line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_fixture {
	unsigned int	samples, frequencies, calls;
	char		order[32];
	unsigned long	rate, interval;
};
static struct mh_field field;

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter value;
	struct clock_fixture *fixture;

	fixture = context;
	fixture->order[fixture->calls++] = 'C';
	value.high = 1;
	value.low = 1000 + fixture->samples++ * fixture->interval;

	return value;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter value;
	struct clock_fixture *fixture;

	fixture = context;
	fixture->order[fixture->calls++] = 'F';
	fixture->frequencies++;
	value.high = 0;
	value.low = fixture->rate;

	return value;
}

static unsigned long
ticks(void *context)
{
	(void)context;

	return 0;
}

int
main(void)
{
	static const struct mh_clock_ops ops = {
		counter, frequency, ticks, NULL, NULL};

	struct clock_fixture first, second;
	struct mh_clock_source source, other_source;
	struct mh_game_timer timer, other_timer;
	struct mh_clock_counter current;
	unsigned long real_ms;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	first.rate = second.rate = 1000;
	first.interval = 250;
	second.interval = 125;
	CHECK(mh_clock_source_init(&source, &ops, &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clock_source_init(&other_source, &ops, &second, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_game_timer_init(&timer, &source, NULL) == MH_ERROR_NONE);
	CHECK(mh_game_timer_init(&other_timer, &other_source, NULL) ==
		MH_ERROR_NONE);
	CHECK(first.calls == 0 && second.calls == 0);

	CHECK(mh_game_timer_start(&timer, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(first.order, "FCFC") == 0);
	CHECK(first.samples == 2 && first.frequencies == 2);
	CHECK(mh_game_timer_ms(&timer) == 250);
	CHECK(mh_game_timer_start(&timer, NULL) == MH_ERROR_NONE);
	CHECK(first.samples == 2 && first.frequencies == 2);
	CHECK(mh_game_timer_step(&timer, NULL) == MH_ERROR_NONE);
	CHECK(first.samples == 3 && first.frequencies == 2);
	CHECK(mh_game_timer_ms(&timer) == 500);

	/* Absolute reads wrap the high word, without advancing game time. */
	CHECK(mh_game_timer_real_word(&timer, &real_ms, NULL) == MH_ERROR_NONE);
	CHECK(real_ms == 1750);
	CHECK(strcmp(first.order, "FCFCCCF") == 0);
	CHECK(mh_game_timer_ms(&timer) == 500);

	/*
	 * Interleaved owners retain independent game time and counter history.
	 */
	CHECK(mh_game_timer_start(&other_timer, NULL) == MH_ERROR_NONE);
	CHECK(mh_game_timer_ms(&other_timer) == 125);
	CHECK(second.samples == 2 && second.frequencies == 2);
	CHECK(mh_game_timer_ms(&timer) == 500);
	CHECK(first.samples == 4 && first.frequencies == 3);

	/* A reset retains the previous anchor, sampling frequency first. */
	CHECK(mh_game_timer_reset(&timer, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(first.order, "FCFCCCFFC") == 0);
	CHECK(mh_game_timer_ms(&timer) == 500);

	field.count = 1;
	field.active_mask = 1;
	field.cars[0].countdown = 1;
	current.high = 1;
	current.low = 2250;
	CHECK(mh_game_timer_enter(&timer, &field, &current, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_game_timer_ms(&timer) == 750);
	CHECK(field.cars[0].frame.seconds == .75);
	CHECK(mh_field_clock_advance(&field, .5L, NULL) == MH_ERROR_NONE);
	current.low = 2750;
	mh_game_timer_leave(&timer, &field, &current);
	CHECK(mh_game_timer_ms(&timer) == 1250);
	CHECK(first.samples == 5 && first.frequencies == 4);

	/* Failed absolute reads preserve the caller's output and game clock. */
	first.rate = 0;
	real_ms = 123;
	CHECK(mh_game_timer_real_word(&timer, &real_ms, NULL) != MH_ERROR_NONE);
	CHECK(real_ms == 123 && mh_game_timer_ms(&timer) == 1250);
	CHECK(mh_game_timer_step(&other_timer, NULL) == MH_ERROR_NONE);
	CHECK(mh_game_timer_ms(&other_timer) == 250);
	CHECK(mh_game_timer_ms(&timer) == 1250);
	CHECK(other_timer.last_race_frame.now_ms == 0);
	CHECK(timer.last_race_frame.now_ms == field.cars[0].frame.time.now_ms);
	puts("Owned game timers preserve sample order, independent state and race handoff");

	return 0;
}
