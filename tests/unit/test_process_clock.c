#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/field.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "process clock line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field field, saved;

int
main(void)
{
	struct mh_clock_accumulator clock, before;
	struct mh_clock_counter current, frequency;
	struct mh_frame_time previous;
	unsigned int i;

	memset(&clock, 0, sizeof(clock));
	current.low = 100;
	current.high = frequency.high = 0;
	frequency.low = 10;
	CHECK(mh_clock_accumulator_reset(&clock, &current, &frequency, NULL) ==
			MH_ERROR_NONE &&
		clock.seconds == 10);
	current.low = 150;
	CHECK(mh_clock_accumulator_reset(&clock, &current, &frequency, NULL) ==
			MH_ERROR_NONE &&
		clock.seconds == 5);
	current.low = 200;
	CHECK(mh_clock_accumulator_step(&clock, &current, 0, NULL) ==
			MH_ERROR_NONE &&
		clock.seconds == 5 && clock.counter.low == 200);
	before = clock;
	frequency.low = 0;
	CHECK(mh_clock_accumulator_reset(&clock, &current, &frequency, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&clock, &before, sizeof(clock)) == 0);
	CHECK(mh_clock_accumulator_step(&clock, NULL, 1, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&clock, &before, sizeof(clock)) == 0);

	memset(&previous, 0, sizeof(previous));
	previous.now_ms = 123000;
	previous.elapsed = .25f;
	field.count = 2;
	field.active_mask = 3;

	for (i = 0; i < 2; i++)
		field.cars[i].countdown = 1;

	saved = field;
	field.cars[1].ticks = 1;
	CHECK(mh_field_clock_load(&field, 5, &previous, NULL) !=
			MH_ERROR_NONE &&
		!field.cars[0].frame.active);
	field = saved;
	CHECK(mh_field_clock_load(&field, 5, &previous, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].frame.intro_ms == 5000 &&
		field.cars[0].frame.time.now_ms == 123000);
	CHECK(field.cars[0].frame.time.elapsed == .25f &&
		!field.cars[0].frame.time.go_ms);
	CHECK(mh_field_clock_advance(&field, .125L, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].frame.time.elapsed == .00001f &&
		field.cars[0].frame.time.now_ms == 5125);
	field = saved;
	field.network = 1;
	CHECK(mh_field_clock_load(&field, 5, &previous, NULL) ==
			MH_ERROR_NONE &&
		field.cars[0].frame.intro_ms == 0);
	field = saved;
	CHECK(mh_field_clock_load(&field, 0, &previous, NULL) ==
			MH_ERROR_NONE &&
		field.cars[0].frame.intro_ms == 0);
	CHECK(mh_field_clock_advance(&field, 20, NULL) == MH_ERROR_NONE &&
		field.cars[0].frame.time.go_ms == 0);
	puts("process clock: retained counter initialization, race-load float history, network/zero intro and rollback pass");

	return 0;
}
