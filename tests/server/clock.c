#include <stdio.h>
#include <string.h>

#include "server/session/clock.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "clock line %d: %s\n", __LINE__, #e);  \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_server_config config;
	struct mh_server_clock clock, before;
	struct mh_server_clock_sample sample;
	struct mh_server_clock_tick tick, saved;

	mh_server_config_defaults(&config);
	config.end_countdown_seconds = 30;
	config.max_race_minutes = 10;
	mh_server_clock_reset(&clock);
	memset(&sample, 0, sizeof(sample));
	sample.racing = 8191;
	sample.countdown_ms = 1000;
	sample.now_ms = 13000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(clock.race_start_ms == 0 && !tick.started);
	sample.now_ms = 13000.25L;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(clock.race_start_ms == 13000 && tick.started &&
		tick.elapsed_ms == 0);
	sample.finished = 2;
	sample.now_ms = 30000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(clock.finish_anchor_ms == 30000 && tick.remaining_seconds == 30);
	CHECK(tick.finish_notice == MH_SERVER_CLOCK_TIME_LEFT &&
		tick.finish_recipients == 8188);
	sample.now_ms = 60000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.finish_notice == MH_SERVER_CLOCK_DID_NOT_FINISH &&
		tick.end == MH_SERVER_CLOCK_RUNNING);
	sample.now_ms = 65000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_RUNNING);
	sample.now_ms = 65000.25L;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_FINISHED);

	/*
	 * All complete shortens the countdown to ten seconds, in either mode.
	 */
	mh_server_clock_reset(&clock);
	sample.finished = sample.racing;
	sample.now_ms = 50000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(clock.all_finished && clock.finish_anchor_ms == 25000);
	sample.now_ms = 60000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_RUNNING && !tick.finish_recipients);
	sample.now_ms += .25L;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_FINISHED);

	mh_server_clock_reset(&clock);
	sample.mode = MH_SERVER_ELIMINATION;
	sample.racing = 4099;
	sample.finished = 2;
	sample.eliminated = 4096;
	sample.now_ms = 50000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(clock.all_finished && clock.finish_anchor_ms == 25000 &&
		!tick.finish_recipients);

	mh_server_clock_reset(&clock);
	clock.race_start_ms = 1000;
	config.max_race_minutes = 1;
	sample.finished = sample.eliminated = 0;
	sample.now_ms = 56000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.limit_notice == MH_SERVER_CLOCK_NOTICE_NONE);
	sample.now_ms += .25L;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.limit_notice == MH_SERVER_CLOCK_RACE_ENDING &&
		tick.limit_recipients == 4098);
	sample.now_ms = 56500;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.limit_notice == MH_SERVER_CLOCK_BLANK);
	sample.now_ms = 61000;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_RUNNING);
	sample.now_ms += .25L;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_NONE);
	CHECK(tick.end == MH_SERVER_CLOCK_TIME_LIMIT);

	before = clock;
	saved = tick;
	sample.now_ms = 1;
	CHECK(mh_server_clock_step(&clock, &config, &sample, &tick, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&before, &clock, sizeof(clock)) &&
		!memcmp(&saved, &tick, sizeof(tick)));

	return 0;
}

int
main(void)
{
	return fixtures();
}
