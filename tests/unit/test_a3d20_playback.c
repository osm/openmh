#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_playback.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "A3D 2.0 playback line %d: %s\n",      \
				__LINE__, #test);                              \
			return 1;                                              \
		}                                                              \
	} while (0)

struct test_clock {
	struct mh_clock_counter	counter;
	unsigned long		increment, calls;
};

static struct mh_clock_counter
counter(void *context)
{
	struct test_clock *clock;
	struct mh_clock_counter result;

	clock = context;
	result = clock->counter;
	clock->counter.low = (result.low + clock->increment) & MH_U32_MAX;
	clock->counter.high =
		(result.high + (clock->counter.low < result.low)) & MH_U32_MAX;
	clock->calls++;

	return result;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = 4000;
	result.high = 0;

	return result;
}

static unsigned long
ticks(void *context)
{
	(void)context;

	return 0;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static int
sequence(void)
{
	struct mh_a3d20_playback_state state;
	struct mh_a3d20_playback_input input;
	struct mh_a3d20_playback_status status;
	struct mh_a3d20_playback_request output;
	struct mh_clock_source source;
	struct test_clock clock;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&status, 0, sizeof(status));
	memset(&clock, 0, sizeof(clock));
	CHECK(mh_clock_source_init(&source, &clock_ops, &clock, NULL) ==
		MH_ERROR_NONE);
	clock.counter.low = 4000;
	input.loaded = input.has_source = input.spatial = 1;
	input.sample_rate = 1000;
	input.pitch = 2;
	input.gain = .1f;
	input.name = "other.wav";
	input.fade_out_samples = 100;
	state.active = 1;
	mh_a3d20_playback_start(&state, &input, &source, &output);
	CHECK(state.skip_update && state.started == 1000);
	CHECK(output.pitch == 2 && output.gain == .3f);

	/* First active pass does not inspect even a stopped status. */
	mh_a3d20_playback_step(&state, &input, &status, &source, &output);
	CHECK(!state.skip_update && output.action == MH_A3D20_PLAYBACK_NONE);
	status.playing = status.looping = status.eligible = 1;
	mh_a3d20_playback_step(&state, &input, &status, &source, &output);
	CHECK(state.fading == 1000);
	clock.counter.low = 4204;
	mh_a3d20_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_A3D20_PLAYBACK_UPDATE && output.gain == 0);

	/* Silent after 50ms; queue expiry is strictly after 50000ms. */
	clock.counter.low = 204000;
	mh_a3d20_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_A3D20_PLAYBACK_UPDATE);
	clock.counter.low++;
	mh_a3d20_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_A3D20_PLAYBACK_STOP && !state.active &&
		!state.fading);
	puts("A3D 2.0 linear gain, startup skip and distinct fade/expiry clocks passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
