#include <stdio.h>
#include <string.h>

#include "client/race/internal.h"
#include "interfaces/clock/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race pacing:%d: %s\n", __LINE__, #x); \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_loop loop;
static struct mh_race_context context;
static unsigned long microseconds;

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = microseconds;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000000;
	value.high = 0;

	return value;
}

static unsigned long
ticks(void *context)
{
	(void)context;

	return microseconds / 1000;
}

int
main(void)
{
	static const struct mh_clock_ops ops = {
		counter, frequency, ticks, NULL, NULL};

	struct mh_clock_source clock;
	struct mh_game_timer timer;
	struct mh_av_settings settings;
	struct mh_race_session_hooks session;

	memset(&timer, 0, sizeof(timer));
	memset(&settings, 0, sizeof(settings));
	memset(&session, 0, sizeof(session));
	CHECK(mh_clock_source_init(&clock, &ops, NULL, NULL) == MH_ERROR_NONE);
	timer.source = &clock;
	context.settings = &settings;
	loop.context = &context;
	loop.timer = &timer;
	loop.success = loop.running = 1;

	/* Uncapped presentation never waits, even at the same instant. */
	CHECK(mh_race_pace_frame(&loop));
	CHECK(mh_race_pace_frame(&loop) && loop.delay == 0);
	settings.max_fps = 200;
	CHECK(mh_race_pace_frame(&loop));
	microseconds = 1000;
	CHECK(!mh_race_pace_frame(&loop) && loop.delay == 4);
	microseconds = 4999;
	loop.delay = 0;
	CHECK(!mh_race_pace_frame(&loop) && loop.delay == 0);
	microseconds = 5000;
	CHECK(mh_race_pace_frame(&loop));

	/* A long frame starts a fresh interval; rendering never catches up. */
	microseconds = 1000000;
	CHECK(mh_race_pace_frame(&loop));
	CHECK(!mh_race_pace_frame(&loop) && loop.delay == 5);
	microseconds = 1005000;
	CHECK(mh_race_pace_frame(&loop));

	/* Reset the source; do not wait for its old absolute deadline. */
	microseconds = 0;
	CHECK(mh_race_pace_frame(&loop));
	settings.max_fps = 1001;
	loop.pace_started = 0;
	CHECK(mh_race_pace_frame(&loop));
	microseconds = 999;
	CHECK(!mh_race_pace_frame(&loop) && loop.delay == 0);
	microseconds = 1000;
	CHECK(mh_race_pace_frame(&loop));

	/* Capture, headless checks and benchmarks ignore the player's cap. */
	loop.capture = "capture";
	CHECK(mh_race_pace_frame(&loop));
	loop.capture = NULL;
	loop.headless = 1;
	CHECK(mh_race_pace_frame(&loop));
	loop.headless = 0;
	session.benchmark = 1;
	loop.session = &session;
	CHECK(mh_race_pace_frame(&loop));
	CHECK(!loop.pace_started && loop.success && loop.running);

	return 0;
}
