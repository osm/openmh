#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/myw.h"
#include "engine/audio/a3d20_pcm.h"
#include "engine/geometry/world.h"
#include "interfaces/clock/backend.h"

#define RATE 8000
#define FRAMES 800
#define VOICES (MH_A3D20_VOICES + 1)
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "A3D 2.0 PCM line %d: %s\n", __LINE__, \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static const unsigned char tone[] = {
	128, 176, 216, 244, 255, 244, 216, 176, 128, 80, 40, 12, 0, 12, 40, 80};

struct fixture {
	struct mh_a3d20_pcm		pcm;
	struct mh_a3d20_queue		queue;
	struct mh_a3d20_voice		voices[VOICES];
	struct mh_a3d20_queue_sink	sink;
	struct mh_a3d20_queue_frame	frame;
	struct mh_clock_source		clock;
	struct mh_audio_pcm		wave;
	float				position[3];
	unsigned long			now;
	float				samples[FRAMES * 2];
};

static struct fixture fixture;

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter result;

	result.low = ((struct fixture *)context)->now;
	result.high = 0;

	return result;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = 1000;
	result.high = 0;

	return result;
}

static unsigned long
ticks(void *context)
{
	return ((struct fixture *)context)->now;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static int
setup(int spatial, int loop)
{
	struct fixture *state;
	unsigned int i;

	state = &fixture;
	mh_a3d20_pcm_destroy(&state->pcm);
	memset(state, 0, sizeof(*state));
	state->now = 1000;
	state->wave.pcm = tone;
	state->wave.frames = state->wave.bytes = sizeof(tone);
	state->wave.channels = 1;
	state->wave.bits = 8;
	state->wave.rate = RATE;
	state->position[1] = -.5f;
	state->position[2] = 20;
	state->frame.listener.position[2] = 10;
	state->frame.listener.front[2] = state->frame.listener.up[1] = 1;
	state->frame.right[0] = 1;
	state->frame.listener.elapsed = .1f;
	state->frame.listener_bound = 1;
	state->frame.master_gain = state->frame.parameters.gain = 1;
	state->frame.clock = &state->clock;
	CHECK(mh_clock_source_init(&state->clock, &clock_ops, state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_a3d20_pcm_init(RATE, .125, &state->pcm, NULL) ==
		MH_ERROR_NONE);
	mh_a3d20_pcm_sink(&state->pcm, &state->sink);

	for (i = 0; i < VOICES; i++) {
		state->voices[i].name = "plain.wav";
		state->voices[i].loaded = state->voices[i].input.has_source = 1;
		state->voices[i].loop = loop;
		state->voices[i].sample_rate = RATE;
		state->voices[i].input.spatial = spatial;
		state->voices[i].input.position = state->position;
		state->voices[i].input.near_distance = 10;
		state->voices[i].input.far_distance = 1000;
		state->voices[i].input.gain = .2f;
		state->voices[i].input.pitch = 1;
		state->voices[i].input.priority = 1;
		CHECK(mh_a3d20_pcm_bind(&state->pcm, i, &state->voices[i],
			&state->wave, NULL) == MH_ERROR_NONE);
	}

	/* Establish the camera history before measuring stationary sources. */
	mh_a3d20_queue_step(&state->queue, &state->frame, &state->sink);

	return 0;
}

static int
render_step(void)
{
	struct fixture *state;

	state = &fixture;
	mh_a3d20_queue_step(&state->queue, &state->frame, &state->sink);
	state->now += 100;
	CHECK(!state->pcm.building);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 0,
		NULL) == MH_ERROR_NONE);

	return 0;
}

static double
channel_energy(unsigned int channel)
{
	double sum;
	unsigned int frame;

	sum = 0;

	for (frame = 0; frame < FRAMES; frame++)
		sum += fixture.samples[frame * 2 + channel] *
			fixture.samples[frame * 2 + channel];

	return sum;
}

static int
positions(void)
{
	struct fixture *state;
	struct mh_a3d20_source_update update;
	unsigned int i;
	double near_energy;

	CHECK(setup(1, 1) == 0);
	state = &fixture;
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	CHECK(state->pcm.output.voices[0].source.position[1] == 0);
	CHECK(state->pcm.output.voices[0].source.position[2] == 20);
	CHECK(state->pcm.output.listener.position[2] == 10);
	near_energy = channel_energy(0);
	CHECK(near_energy > 0 && near_energy == channel_energy(1));

	/* Admission's undefined zero-time velocity must not reach the mixer. */
	for (i = 0; i < 3; i++)
		CHECK(state->pcm.output.voices[0].source.velocity[i] == 0);

	state->position[2] = 30;
	CHECK(render_step() == 0);
	CHECK(render_step() == 0);
	mh_a3d20_queue_clear(&state->queue, &state->sink);
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	CHECK(fabs(channel_energy(0) / near_energy - .25) < .002);
	state->position[0] = 20;
	state->position[2] = 10;
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) > 0);
	state->frame.listener.front[2] = -1;
	CHECK(render_step() == 0);
	CHECK(channel_energy(1) == 0 && channel_energy(0) > 0);

	memset(&update, 0, sizeof(update));
	update.changed = 1;
	update.position[2] = -25;
	update.velocity[0] = 3;
	update.velocity[1] = 4;
	update.velocity[2] = -5;
	state->sink.source(state->sink.context, &state->voices[0], &update);
	CHECK(state->pcm.output.voices[0].source.velocity[2] == 5);
	update.position[2] = -26;
	update.velocity_kind[0] = MH_A3D20_VELOCITY_UNDEFINED;
	update.velocity_kind[1] = MH_A3D20_VELOCITY_POSITIVE_INFINITY;
	update.velocity_kind[2] = MH_A3D20_VELOCITY_NEGATIVE_INFINITY;
	state->sink.source(state->sink.context, &state->voices[0], &update);
	CHECK(state->pcm.output.voices[0].source.position[2] == 26);
	CHECK(state->pcm.output.voices[0].source.velocity[0] == 3 &&
		state->pcm.output.voices[0].source.velocity[1] == 4 &&
		state->pcm.output.voices[0].source.velocity[2] == 5);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	/*
	 * Listener-relative sounds are positional sources, not log-pan buffers.
	 */
	CHECK(setup(0, 1) == 0);
	state->voices[0].input.pan = 1;
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	CHECK(state->pcm.output.voices[0].spatial && channel_energy(0) == 0 &&
		channel_energy(1) > 0);
	state->frame.listener.position[0] = 10;
	CHECK(render_step() == 0);
	CHECK(state->pcm.output.voices[0].source.position[0] == 11);
	CHECK(state->pcm.output.voices[0].source.velocity[0] ==
		state->pcm.output.listener.velocity[0]);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	return 0;
}

static int
linear_levels(void)
{
	static unsigned char flat[16];
	struct fixture *state;
	struct mh_a3d20_playback_request request;
	struct mh_mixer_voice *voice;
	double expected, cursor, before;

	CHECK(setup(1, 1) == 0);
	state = &fixture;
	memset(flat, 192, sizeof(flat));
	state->wave.pcm = flat;
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	CHECK(!state->voices[0].playback.skip_update);
	memset(&request, 0, sizeof(request));
	request.action = MH_A3D20_PLAYBACK_UPDATE;
	request.gain = .123456f;
	request.pitch = 1.00003125f;
	state->sink.playback(state->sink.context, &state->voices[0], &request);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 0,
		NULL) == MH_ERROR_NONE);
	expected = .5 * (double)request.gain * .125 * sqrt(.5);
	CHECK(fabs(state->samples[0] - expected) < .00000001);
	voice = &state->pcm.output.mixer.voices[0];
	CHECK(voice->pitch == (double)request.pitch);
	CHECK(voice->cursor > 0 && voice->cursor < .1);
	cursor = voice->cursor;
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0 &&
		voice->cursor == cursor);

	request.gain = 0;
	state->sink.playback(state->sink.context, &state->voices[0], &request);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && voice->active);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	/* Sample tuning stays linear: gason is 1.6 times plain. */
	CHECK(setup(1, 1) == 0);
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	before = channel_energy(0);
	mh_a3d20_queue_clear(&state->queue, &state->sink);
	state->voices[0].name = "gason.wav";
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	CHECK(fabs(channel_energy(0) / before - 2.56) < .0001);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	return 0;
}

static int
transitions(void)
{
	static struct mh_a3d20_pcm partitioned;
	struct fixture *state;
	struct mh_a3d20_playback_request request;
	float whole[80], chunks[80];
	double cursor;

	CHECK(setup(1, 1) == 0);
	state = &fixture;
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	state->pcm.output.transition_frames = 40;
	memset(&request, 0, sizeof(request));
	request.action = MH_A3D20_PLAYBACK_UPDATE;
	request.gain = .123456f;
	request.pitch = 1.50003125f;
	state->sink.playback(state->sink.context, &state->voices[0], &request);
	CHECK(mh_a3d20_pcm_init(RATE, .125, &partitioned, NULL) ==
		MH_ERROR_NONE);
	partitioned.output = state->pcm.output;
	CHECK(mh_a3d20_pcm_render(&state->pcm, whole, 40, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_a3d20_pcm_render(&partitioned, chunks, 7, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = partitioned.output.mixer.voices[0].cursor;
	CHECK(mh_a3d20_pcm_render(&partitioned, state->samples, FRAMES, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);
	CHECK(partitioned.output.mixer.voices[0].cursor == cursor);
	CHECK(partitioned.output.mixer.voices[0].transition == 33);
	CHECK(mh_a3d20_pcm_render(&partitioned, chunks + 14, 13, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_a3d20_pcm_render(&partitioned, chunks + 40, 20, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(whole, chunks, sizeof(whole)) == 0);
	CHECK(state->pcm.output.mixer.voices[0].pitch == (double)request.pitch);
	CHECK(state->pcm.output.mixer.voices[0].transition == 0);
	mh_a3d20_pcm_destroy(&partitioned);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	return 0;
}

static int
lifetime(void)
{
	struct fixture *state;
	struct mh_a3d20_playback_status status;
	unsigned int i, active;

	CHECK(setup(1, 0) == 0);
	state = &fixture;
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(render_step() == 0);
	memset(&status, 0, sizeof(status));
	state->sink.status(state->sink.context, &state->voices[0], &status);
	CHECK(!status.playing && state->voices[0].playback.active);
	CHECK(render_step() == 0);
	CHECK(!state->voices[0].playback.active && state->voices[0].slot == 0);

	CHECK(setup(1, 1) == 0);

	/* Match admission scratch gain to each source's published gain. */
	state->frame.parameters.gain = .6f;

	for (i = 0; i < VOICES; i++) {
		state->voices[i].input.priority =
			i == MH_A3D20_VOICES ? 100 : 1;
		mh_a3d20_queue_request(
			&state->queue, &state->voices[i], &state->sink);
	}

	CHECK(render_step() == 0);
	active = 0;

	for (i = 0; i < VOICES; i++)
		active += state->voices[i].playback.active != 0;

	CHECK(active == MH_A3D20_VOICES && !state->voices[0].playback.active);
	CHECK(state->pcm.output.voices[0].channel == 0 &&
		state->pcm.output.voices[MH_A3D20_VOICES].channel != 0);
	mh_a3d20_queue_clear(&state->queue, &state->sink);
	CHECK(render_step() == 0 && channel_energy(0) == 0 &&
		channel_energy(1) == 0);

	/* Duplicate sample payloads retain independent playback cursors. */
	CHECK(setup(1, 1) == 0);
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	mh_a3d20_queue_step(&state->queue, &state->frame, &state->sink);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, 3, 0, NULL) ==
		MH_ERROR_NONE);
	mh_a3d20_queue_request(&state->queue, &state->voices[1], &state->sink);
	mh_a3d20_queue_step(&state->queue, &state->frame, &state->sink);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, 3, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(state->pcm.output.mixer.voices[0].cursor == 6 &&
		state->pcm.output.mixer.voices[1].cursor == 3);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	return 0;
}

static int
errors(void)
{
	struct fixture *state;
	struct mh_a3d20_playback_request request;
	unsigned int i;

	CHECK(setup(0, 1) == 0);
	state = &fixture;
	state->wave.channels = 2;
	state->wave.frames /= 2;
	CHECK(mh_a3d20_pcm_bind(&state->pcm, 0, &state->voices[0], &state->wave,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(setup(1, 1) == 0);
	mh_a3d20_queue_request(&state->queue, &state->voices[0], &state->sink);
	CHECK(mh_a3d20_pcm_bind(&state->pcm, 0, &state->voices[0], &state->wave,
		NULL) == MH_ERROR_STATE);
	CHECK(render_step() == 0);
	state->pcm.geometry.count = 1;
	state->sink.begin(state->sink.context, NULL);
	CHECK(state->pcm.geometry.count == 0);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 0,
		NULL) == MH_ERROR_STATE);
	state->sink.flush(state->sink.context);
	memset(&request, 0, sizeof(request));
	request.action = MH_A3D20_PLAYBACK_UPDATE;
	request.gain = -1;
	request.pitch = 1;
	state->sink.playback(state->sink.context, &state->voices[0], &request);
	CHECK(mh_a3d20_pcm_render(&state->pcm, state->samples, FRAMES, 0,
		NULL) == MH_ERROR_INVALID);
	mh_a3d20_queue_clear(&state->queue, &state->sink);

	for (i = 0; i < MH_MIXER_VOICES; i++)
		CHECK(!state->pcm.output.mixer.voices[i].active);

	return 0;
}

int
main(void)
{

	if (positions() || linear_levels() || transitions() || lifetime() ||
		errors())
		return 1;

	mh_a3d20_pcm_destroy(&fixture.pcm);
	puts("A3D 2.0 direct PCM: linear levels, handedness, velocity rejection, 24 slots, pause and lifetime pass");

	return 0;
}
