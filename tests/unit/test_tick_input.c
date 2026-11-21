#include <stdio.h>
#include <string.h>

#include "engine/input/tick_input.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "tick input line %d: %s\n", __LINE__,  \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_tick_input input, saved;

int
main(void)
{
	struct mh_tick_input_sample sample, actual;
	unsigned int i;

	mh_tick_input_init(&input);
	memset(&sample, 0, sizeof(sample));
	sample.enabled = 1;
	sample.fallback.throttle = 1;
	CHECK(mh_tick_input_write(&input, 2, &sample, NULL) == MH_ERROR_NONE);

	/* A shift tap entirely between ticks must still reach the engine. */
	sample.shift_held = MH_TICK_SHIFT_UP;
	CHECK(mh_tick_input_write(&input, 2, &sample, NULL) == MH_ERROR_NONE);
	sample.shift_held = 0;
	CHECK(mh_tick_input_write(&input, 2, &sample, NULL) == MH_ERROR_NONE);

	/* Fresh input after a stall must not rewrite the overdue ticks. */
	sample.fallback.throttle = 0;
	sample.fallback.brake = 1;
	sample.reset = 1;
	CHECK(mh_tick_input_write(&input, 20, &sample, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_input_read(&input, 1, &actual, NULL) == MH_ERROR_NONE);
	CHECK(!actual.enabled && actual.fallback.throttle == 0);
	CHECK(mh_tick_input_read(&input, 2, &actual, NULL) == MH_ERROR_NONE);
	CHECK(actual.fallback.throttle == 1 &&
		actual.shift_pressed == MH_TICK_SHIFT_UP);

	for (i = 3; i < 20; i++) {
		CHECK(mh_tick_input_read(&input, i, &actual, NULL) ==
			MH_ERROR_NONE);
		CHECK(actual.fallback.throttle == 1 &&
			actual.shift_pressed == 0);
		CHECK(actual.reset == 0);
	}

	CHECK(mh_tick_input_read(&input, 20, &actual, NULL) == MH_ERROR_NONE);
	CHECK(actual.fallback.throttle == 0 && actual.fallback.brake == 1);
	CHECK(actual.reset == 1);
	CHECK(mh_tick_input_read(&input, 21, &actual, NULL) == MH_ERROR_NONE);
	CHECK(actual.reset == 0 && actual.fallback.brake == 1);
	saved = input;
	CHECK(mh_tick_input_write(&input, 21, &sample, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_tick_input_read(&input, 23, &actual, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&input, &saved, sizeof(input)) == 0);
	mh_tick_input_init(&input);

	for (i = 1; i <= MH_TICK_INPUT_CAPACITY; i++)
		CHECK(mh_tick_input_write(&input, i, &sample, NULL) ==
			MH_ERROR_NONE);

	saved = input;
	CHECK(mh_tick_input_write(&input, MH_TICK_INPUT_CAPACITY + 1, &sample,
		NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&input, &saved, sizeof(input)) == 0);

	for (i = 1; i <= MH_TICK_INPUT_CAPACITY; i++) {
		CHECK(mh_tick_input_read(&input, i, &actual, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_tick_input_write(&input, MH_TICK_INPUT_CAPACITY + i,
			&sample, NULL) == MH_ERROR_NONE);
	}

	mh_tick_input_init(&input);
	memset(&sample, 0, sizeof(sample));
	sample.enabled = 1;
	sample.shift_pressed = MH_TICK_SHIFT_DOWN;
	CHECK(mh_tick_input_write(&input, 1, &sample, NULL) == MH_ERROR_NONE);
	CHECK(mh_tick_input_read(&input, 1, &actual, NULL) == MH_ERROR_NONE);
	CHECK(actual.shift_held == 0 &&
		actual.shift_pressed == MH_TICK_SHIFT_DOWN);
	CHECK(mh_tick_input_read(&input, 2, &actual, NULL) == MH_ERROR_NONE);
	CHECK(actual.shift_pressed == 0);
	puts("tick input: held state, short taps, backlog, edges and bounded overflow passed");

	return 0;
}
