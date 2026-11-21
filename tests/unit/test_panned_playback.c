#include <stdio.h>
#include <string.h>

#ifdef MH_TEST_A3D12
#include "engine/audio/a3d12_playback.h"
#define PLAYBACK_START mh_a3d12_playback_start
#define PLAYBACK_STEP mh_a3d12_playback_step
#define PLAYBACK_STOP mh_a3d12_playback_stop
#else
#include "engine/audio/ds3d_playback.h"
#define PLAYBACK_START mh_ds3d_playback_start
#define PLAYBACK_STEP mh_ds3d_playback_step
#define PLAYBACK_STOP mh_ds3d_playback_stop
#endif
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "Panned playback line %d: %s\n",       \
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
check_sequence(void)
{
	struct mh_spatial_playback_state state;
	struct mh_spatial_playback_input input;
	struct mh_spatial_playback_status status;
	struct mh_spatial_playback_request output;
	struct mh_clock_source source;
	struct test_clock clock;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&status, 0, sizeof(status));
	memset(&clock, 0, sizeof(clock));
	CHECK(mh_clock_source_init(&source, &clock_ops, &clock, NULL) ==
		MH_ERROR_NONE);
	input.spatial = 1;
	input.loaded = input.has_buffer = state.active = 1;
	input.frequency = 1000;
	input.gain = .1f;
	input.fade_in_samples = input.fade_out_samples = 100;
	clock.counter.low = 4000;
	PLAYBACK_START(&state, &input, &source, &output);
	CHECK(output.action == MH_SPATIAL_PLAYBACK_START &&
		output.volume == -10000 && state.started == 1000 &&
		state.active == 1 && clock.calls == 2);

	clock.counter.low = 4200;
	status.playing = status.eligible = 1;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_SPATIAL_PLAYBACK_UPDATE &&
		output.volume == -5945);

	clock.counter.low = 4400;
	status.looping = 1;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(state.fading == 1100 &&
		output.action == MH_SPATIAL_PLAYBACK_UPDATE);
	clock.counter.low = 4800;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_SPATIAL_PLAYBACK_UPDATE &&
		output.volume == -10000);
	clock.counter.low++;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_SPATIAL_PLAYBACK_STOP && !state.active &&
		!state.started && !state.fading);

	state.active = 1;
	input.loop = 1;
	PLAYBACK_START(&state, &input, &source, &output);
	CHECK(output.loop == 1);
	state.fading = 1000;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(state.fading == 0 && output.action == MH_SPATIAL_PLAYBACK_UPDATE);
	status.playing = 0;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_SPATIAL_PLAYBACK_STOP && !state.active);
	status.playing = 1;
	input.spatial = 0;
	input.fade_in_samples = 0;
	input.prepared_gain = .1f;
	input.pan = -.05f;
	PLAYBACK_START(&state, &input, &source, &output);
	CHECK(output.set_pan && output.pan == -4054 && output.volume == -5299);
	input.gain = 10;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(output.set_pan && output.pan == -4054 && output.volume == -5299);
	input.spatial = 1;
	PLAYBACK_STEP(&state, &input, &status, &source, &output);
	CHECK(!output.set_pan && output.volume == 0);
	puts("DS3D gain/pan, fades, strict expiry, loop reset and stop cleanup passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}
