#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_playback.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "EAX playback line %d: %s\n",          \
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
	struct mh_eax_playback_state state;
	struct mh_eax_playback_input input;
	struct mh_eax_playback_status status;
	struct mh_eax_playback_request output;
	struct mh_clock_source source;
	struct test_clock clock;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&status, 0, sizeof(status));
	memset(&clock, 0, sizeof(clock));
	CHECK(mh_clock_source_init(&source, &clock_ops, &clock, NULL) ==
		MH_ERROR_NONE);
	input.name = "other.wav";
	input.loaded = input.has_buffer = state.active = 1;
	input.frequency = 1000;
	input.gain = .1f;
	input.fade_in_samples = input.fade_out_samples = 100;
	clock.counter.low = 4000;
	mh_eax_playback_start(&state, &input, &source, &output);
	CHECK(output.action == MH_EAX_PLAYBACK_START &&
		output.volume == -10000 && state.started == 1000 &&
		state.active == 1 && clock.calls == 2);

	clock.counter.low = 4200;
	status.playing = status.eligible = 1;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_EAX_PLAYBACK_UPDATE &&
		output.volume == -5945);

	clock.counter.low = 4400;
	status.looping = 1;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(state.fading == 1100 && output.action == MH_EAX_PLAYBACK_UPDATE);
	clock.counter.low = 4800;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_EAX_PLAYBACK_UPDATE &&
		output.volume == -10000);
	clock.counter.low++;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_EAX_PLAYBACK_STOP && !state.active &&
		!state.started && !state.fading);

	state.active = 1;
	input.loop = 1;
	mh_eax_playback_start(&state, &input, &source, &output);
	CHECK(output.loop == 1);
	state.fading = 1000;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(state.fading == 0 && output.action == MH_EAX_PLAYBACK_UPDATE);
	status.playing = 0;
	mh_eax_playback_step(&state, &input, &status, &source, &output);
	CHECK(output.action == MH_EAX_PLAYBACK_STOP && !state.active);
	puts("EAX playback attack/release, strict expiry, loop reset and stop cleanup passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}
