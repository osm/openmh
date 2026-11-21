#include <stdio.h>
#include <string.h>

#include "engine/simulation/field.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "fixed frame line %d: %s\n", __LINE__, \
				#expression);                                  \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field field, saved;

static int
fixed_rate_clock(unsigned int rate)
{
	struct mh_drive *drive;
	struct mh_drive_frame frame;
	unsigned int tick;

	memset(&field, 0, sizeof(field));
	field.count = 1;
	drive = &field.cars[0];
	drive->seconds = drive->frame.seconds = 100000;
	drive->frame.active = 1;
	drive->frame.time.now_ms = 100000000;
	drive->frame.intro_ms = drive->frame.time.go_ms = 100000000;
	CHECK(mh_field_simulation_rate(&field, rate, NULL) == MH_ERROR_NONE);

	for (tick = 1; tick <= rate * 10; tick++) {
		CHECK(mh_field_clock_advance(&field, 1.0L / rate, NULL) ==
			MH_ERROR_NONE);
		CHECK(drive->frame.time.elapsed == (float)(1.0L / rate));
		CHECK(drive->frame.seconds ==
			(double)(100000.0L + (long double)tick / rate));
		CHECK(drive->seconds == drive->frame.seconds);
	}

	CHECK(drive->frame.fixed_ticks == rate * 10);
	saved = field;
	CHECK(mh_field_clock_advance(&field, .5L / rate, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_field_simulation_rate(&field, 120, NULL) == MH_ERROR_STATE);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);
	CHECK(mh_drive_frame_sample(drive, 0, &frame, NULL) == MH_ERROR_NONE);
	CHECK(frame.fixed_ticks == drive->frame.fixed_ticks &&
		frame.time.elapsed == 0);
	CHECK(mh_field_clock_stage(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_restart_clock(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(drive->frame.fixed_ticks == 0);
	CHECK(mh_field_clock_advance(&field, 1.0L / rate, NULL) ==
		MH_ERROR_NONE);
	CHECK(drive->frame.fixed_ticks == 1);
	CHECK(drive->frame.time.elapsed == (float)(1.0L / rate));

	return 0;
}

int
main(void)
{
	const unsigned int rates[] = {60, 120, 240, 100, 144};
	unsigned int i;

	for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		if (fixed_rate_clock(rates[i]) != 0) {
			fprintf(stderr, "fixed field clock: simulation=%u Hz\n",
				rates[i]);
			return 1;
		}
	}

	puts("fixed field clock: exact dt, absolute time, restart and atomic errors passed");

	return 0;
}
