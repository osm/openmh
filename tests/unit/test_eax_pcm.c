#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_pcm.h"
#include "interfaces/clock/backend.h"

#define RATE 8000
#define FRAMES 800
#define VOICES (MH_EAX_VOICES + 1)
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "EAX PCM line %d: %s\n", __LINE__,     \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static const unsigned char tone[] = {
	128, 176, 216, 244, 255, 244, 216, 176, 128, 80, 40, 12, 0, 12, 40, 80};

struct fixture {
	struct mh_eax_pcm		pcm;
	struct mh_eax_queue		queue;
	struct mh_eax_voice		voices[VOICES];
	struct mh_eax_queue_sink	sink;
	struct mh_eax_queue_frame	frame;
	struct mh_eax_source_listener	source_listener;
	struct mh_eax_listener_state	listener_state;
	struct mh_eax_listener_input	listener_input;
	struct mh_clock_source		clock;
	struct mh_audio_pcm		wave;
	unsigned long			now;
	float				samples[FRAMES * 2];
};

static struct fixture fixture;
static float reference[FRAMES * 2];

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter value;

	value.low = ((struct fixture *)context)->now;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static unsigned long
ticks(void *context)
{
	return ((struct fixture *)context)->now;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static void
cleanup(void)
{
	struct fixture *test;

	test = &fixture;

	if (test->pcm.reverb != NULL) {
		mh_eax_queue_clear(&test->queue, &test->sink);
		mh_eax_pcm_destroy(&test->pcm);
	}
}

static int
setup(int spatial, int loop, float reverb)
{
	struct fixture *test;
	struct mh_eax_voice *voice;
	unsigned int record_index;

	cleanup();
	test = &fixture;
	memset(test, 0, sizeof(*test));
	test->now = 1000;
	test->wave.pcm = tone;
	test->wave.bytes = test->wave.frames = sizeof(tone);
	test->wave.rate = RATE;
	test->wave.channels = 1;
	test->wave.bits = 8;
	test->source_listener.position[2] = test->source_listener.previous[2] =
		10;
	test->source_listener.right[0] = 1;
	test->listener_input.elapsed = .04f;
	test->listener_input.position[2] = 10;
	test->listener_input.front[2] = 1;
	test->listener_input.up[1] = 1;
	test->listener_input.reverb = reverb;
	test->frame.listener = &test->source_listener;
	test->frame.master_gain = test->frame.parameters.gain = 1;
	test->frame.clock = &test->clock;
	CHECK(mh_clock_source_init(&test->clock, &clock_ops, test, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_eax_pcm_init(RATE, .125, &test->pcm, NULL) == MH_ERROR_NONE);
	mh_eax_pcm_sink(&test->pcm, &test->sink);

	for (record_index = 0; record_index < VOICES; record_index++) {
		voice = &test->voices[record_index];
		voice->name = "tone.wav";
		voice->loaded = voice->has_buffer = voice->input.has_buffer = 1;
		voice->loop = loop;
		voice->input.spatial = spatial;
		voice->input.sample_rate = RATE;
		voice->input.gain = .5f;
		voice->input.pitch = 1;
		voice->input.elapsed = test->listener_input.elapsed;
		voice->input.priority = 1;
		voice->input.near_distance = 10;
		voice->input.far_distance = 1000;
		voice->input.position[2] = 20;
		CHECK(mh_eax_pcm_bind(&test->pcm, record_index, voice,
			&test->wave, NULL) == MH_ERROR_NONE);
	}

	return 0;
}

static int
step(void)
{
	struct fixture *test;
	struct mh_eax_listener_update update;

	test = &fixture;
	mh_eax_queue_step(&test->queue, &test->frame, &test->sink);
	mh_eax_listener_step(
		&test->listener_state, &test->listener_input, &update);
	CHECK(mh_eax_pcm_listener(&test->pcm, &update, NULL) == MH_ERROR_NONE);
	test->now += 40;

	return 0;
}

static int
render(void)
{
	CHECK(mh_eax_pcm_render(&fixture.pcm, fixture.samples, FRAMES, 0,
		NULL) == MH_ERROR_NONE);

	return 0;
}

static double
energy(unsigned int channel)
{
	double sum;
	size_t frame_index;

	sum = 0;

	for (frame_index = 0; frame_index < FRAMES; frame_index++)
		sum += (double)fixture.samples[frame_index * 2 + channel] *
			fixture.samples[frame_index * 2 + channel];

	return sum;
}

static int
source_levels(void)
{
	struct fixture *test;
	double near_energy, gain_energy, ratio;
	long volume;

	CHECK(setup(1, 1, 0) == 0);
	test = &fixture;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	near_energy = energy(0);
	volume = test->pcm.output.voices[0].volume;
	CHECK(near_energy > 0 && energy(0) == energy(1));
	CHECK(setup(1, 1, 0) == 0);
	test->voices[0].input.position[2] = 30;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	CHECK(fabs(energy(0) / near_energy - .25) < .001);
	CHECK(setup(1, 1, 0) == 0);
	test->voices[0].name = "asfalt2.wav";
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	gain_energy = energy(0);
	CHECK(test->pcm.output.voices[0].volume < volume);
	ratio = pow(10, (test->pcm.output.voices[0].volume - volume) / 1000.0);
	CHECK(fabs(gain_energy / near_energy - ratio) < .00001);

	/* EAX listener-relative sounds use positional buffers, not DS3D pan. */
	CHECK(setup(0, 1, 0) == 0);
	test->voices[0].input.pan = 1;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	CHECK(energy(0) == 0 && energy(1) > 0);
	CHECK(test->pcm.output.voices[0].source.position[0] == 1);
	test->wave.channels = 2;
	test->wave.frames /= 2;
	CHECK(mh_eax_pcm_bind(&test->pcm, 1, &test->voices[1], &test->wave,
		NULL) == MH_ERROR_UNSUPPORTED);

	return 0;
}

static int
listener_motion(void)
{
	struct fixture *test;
	struct mh_eax_listener_update update;
	double right_energy;

	CHECK(setup(1, 1, 0) == 0);
	test = &fixture;
	test->voices[0].input.position[0] = 10;
	test->voices[0].input.position[2] = 10;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	right_energy = energy(1);
	CHECK(energy(0) == 0 && right_energy > 0);
	test->listener_input.front[2] = -1;
	CHECK(step() == 0 && render() == 0);
	CHECK(energy(1) == 0 && fabs(energy(0) - right_energy) < .00001);
	CHECK(setup(1, 1, 0) == 0);
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	CHECK(test->pcm.output.mixer.voices[0].pitch == 1);
	test->listener_input.position[2] += 1;
	CHECK(step() == 0 && render() == 0);
	CHECK(test->pcm.output.mixer.voices[0].pitch > 1);

	/* Zero-time requests must leave the committed pose/velocity intact. */
	test->listener_input.elapsed = 0;
	test->listener_input.position[2] = 100;
	mh_eax_listener_step(
		&test->listener_state, &test->listener_input, &update);
	CHECK(mh_eax_pcm_listener(&test->pcm, &update, NULL) == MH_ERROR_NONE);
	CHECK(test->pcm.output.listener.position[2] == 11);

	return 0;
}

static int
admission_and_status(void)
{
	struct fixture *test;
	struct mh_eax_playback_status status;
	unsigned int record_index, voice_index, active;

	CHECK(setup(1, 1, 0) == 0);
	test = &fixture;

	for (record_index = 0; record_index < VOICES; record_index++) {
		test->voices[record_index].input.priority =
			record_index == MH_EAX_VOICES ? 100 : 1;
		mh_eax_queue_request(
			&test->queue, &test->voices[record_index], &test->sink);
	}

	CHECK(step() == 0 && render() == 0);
	active = 0;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		active +=
			test->pcm.output.mixer.voices[voice_index].active != 0;

	CHECK(active == MH_EAX_VOICES);
	CHECK(test->pcm.output.voices[0].channel == 0);
	CHECK(test->pcm.output.voices[MH_EAX_VOICES].channel != 0);
	memset(&status, 0, sizeof(status));
	test->sink.status(
		test->sink.context, &test->voices[MH_EAX_VOICES], &status);
	CHECK(status.playing && status.looping);
	mh_eax_queue_clear(&test->queue, &test->sink);
	mh_eax_pcm_clear(&test->pcm);
	CHECK(render() == 0 && energy(0) == 0 && energy(1) == 0);
	CHECK(mh_eax_pcm_bind(&test->pcm, 0, &test->voices[0], &test->wave,
		NULL) == MH_ERROR_NONE);

	return 0;
}

static int
tail_and_pause(void)
{
	struct fixture *test;
	struct mh_eax_playback_status status;
	double cursor;

	CHECK(setup(1, 0, 1) == 0);
	test = &fixture;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	memset(&status, 0, sizeof(status));
	test->sink.status(test->sink.context, &test->voices[0], &status);
	CHECK(!status.playing && !status.looping);
	CHECK(step() == 0 && !test->voices[0].playback.active);
	CHECK(render() == 0 && energy(0) > 0 && energy(1) > 0);
	memcpy(reference, test->samples, sizeof(reference));
	CHECK(setup(1, 0, 1) == 0);
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0 && step() == 0);
	CHECK(mh_eax_pcm_render(&test->pcm, test->samples, FRAMES, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	CHECK(render() == 0 &&
		memcmp(reference, test->samples, sizeof(reference)) == 0);
	test->pcm.volume = 0;
	CHECK(render() == 0 && energy(0) == 0 && energy(1) == 0);
	test->pcm.volume = 1;
	CHECK(render() == 0 && energy(0) > 0);
	mh_eax_pcm_clear(&test->pcm);
	CHECK(render() == 0 && energy(0) == 0 && energy(1) == 0);
	CHECK(setup(1, 1, 1) == 0);
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0 && render() == 0);
	cursor = test->pcm.output.mixer.voices[0].cursor;
	CHECK(mh_eax_pcm_render(&test->pcm, test->samples, FRAMES, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(test->pcm.output.mixer.voices[0].cursor == cursor);

	return 0;
}

static int
partition_and_errors(void)
{
	struct fixture *test;
	struct mh_eax_voice unbound;
	struct mh_spatial_source_update source;
	struct mh_eax_listener_update listener;
	unsigned int voice_index;
	size_t offset, frames;

	CHECK(setup(1, 1, 1) == 0);
	test = &fixture;
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(mh_eax_pcm_bind(&test->pcm, 0, &test->voices[0], &test->wave,
		NULL) == MH_ERROR_STATE);
	CHECK(step() == 0 && render() == 0);
	memcpy(reference, test->samples, sizeof(reference));
	CHECK(setup(1, 1, 1) == 0);
	mh_eax_queue_request(&test->queue, &test->voices[0], &test->sink);
	CHECK(step() == 0);

	for (offset = 0; offset < FRAMES; offset += frames) {
		frames = FRAMES - offset;

		if (frames > 37)
			frames = 37;

		CHECK(mh_eax_pcm_render(&test->pcm, test->samples + offset * 2,
			frames, 0, NULL) == MH_ERROR_NONE);
	}

	CHECK(memcmp(reference, test->samples, sizeof(reference)) == 0);
	memset(&unbound, 0, sizeof(unbound));
	memset(&source, 0, sizeof(source));
	test->sink.source(test->sink.context, &unbound, &source);
	CHECK(mh_eax_pcm_render(&test->pcm, test->samples, FRAMES, 0, NULL) ==
		MH_ERROR_STATE);
	mh_eax_queue_clear(&test->queue, &test->sink);

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		CHECK(!test->pcm.output.mixer.voices[voice_index].active);

	CHECK(setup(1, 1, 0) == 0);
	memset(&listener, 0, sizeof(listener));
	listener.reverb_changed = 1;
	listener.reverb = 2;
	CHECK(mh_eax_pcm_listener(&test->pcm, &listener, NULL) ==
		MH_ERROR_INVALID);
	listener.reverb = 0;
	CHECK(mh_eax_pcm_listener(&test->pcm, &listener, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_eax_pcm_render(&test->pcm, test->samples, FRAMES, 0, NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

int
main(void)
{
	int failed;

	failed = source_levels() || listener_motion() ||
		admission_and_status() || tail_and_pause() ||
		partition_and_errors();
	cleanup();

	if (failed)
		return 1;

	puts("EAX PCM: source gains, relative buffers, 24 voices, completion, reverb tails, pause, mute, block independence and cleanup passed");

	return 0;
}
