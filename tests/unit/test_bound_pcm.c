#include <math.h>
#include <stdio.h>
#include <string.h>

/* Run identical PCM scenarios through each profile's own queue adapter. */
#ifdef MH_TEST_A3D12
#include "engine/audio/a3d12_pcm.h"
#define PCM mh_a3d12_pcm
#define QUEUE mh_a3d12_queue
#define VOICE mh_a3d12_voice
#define QUEUE_FRAME mh_spatial_queue_frame
#define QUEUE_SINK mh_a3d12_queue_sink
#define VOICE_COUNT MH_A3D12_VOICES
#define PCM_INIT mh_a3d12_pcm_init
#define PCM_BIND mh_a3d12_pcm_bind
#define PCM_SINK mh_a3d12_pcm_sink
#define PCM_RENDER mh_a3d12_pcm_render
#define QUEUE_REQUEST mh_a3d12_queue_request
#define QUEUE_CLEAR mh_a3d12_queue_clear
#define QUEUE_STEP mh_a3d12_queue_step
#else
#include "engine/audio/ds3d_pcm.h"
#define PCM mh_ds3d_pcm
#define QUEUE mh_ds3d_queue
#define VOICE mh_ds3d_voice
#define QUEUE_FRAME mh_spatial_queue_frame
#define QUEUE_SINK mh_ds3d_queue_sink
#define VOICE_COUNT MH_DS3D_VOICES
#define PCM_INIT mh_ds3d_pcm_init
#define PCM_BIND mh_ds3d_pcm_bind
#define PCM_SINK mh_ds3d_pcm_sink
#define PCM_RENDER mh_ds3d_pcm_render
#define QUEUE_REQUEST mh_ds3d_queue_request
#define QUEUE_CLEAR mh_ds3d_queue_clear
#define QUEUE_STEP mh_ds3d_queue_step
#endif
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define FRAMES 800
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "Spatial PCM line %d: %s\n", __LINE__, \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static const unsigned char tone[] = {
	128, 176, 216, 244, 255, 244, 216, 176, 128, 80, 40, 12, 0, 12, 40, 80};

struct fixture {
	struct PCM		output;
	struct QUEUE		queue;
	struct VOICE		voice;
	struct QUEUE_FRAME	frame;
	struct QUEUE_SINK	sink;
	struct mh_clock_source	clock;
	struct mh_audio_pcm	wave;
	unsigned long		now;
	float			pcm[FRAMES * 2];
};

static struct fixture fixture;

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter value;
	struct fixture *state;

	state = context;
	value.low = state->now;
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

static int
setup_fixture(int spatial, int loop)
{
	struct fixture *state;

	state = &fixture;
	memset(state, 0, sizeof(*state));
	state->now = 1000;
	state->wave.pcm = tone;
	state->wave.bytes = state->wave.frames = sizeof(tone);
	state->wave.rate = 8000;
	state->wave.channels = 1;
	state->wave.bits = 8;
	state->voice.common.loaded = state->voice.common.has_buffer = 1;
	state->voice.common.loop = loop;
	state->voice.common.input.spatial = spatial;
	state->voice.common.input.has_buffer = spatial;
	state->voice.common.input.sample_rate = state->wave.rate;
	state->voice.common.input.gain = .5f;
	state->voice.common.input.pitch = 1;
	state->voice.common.input.near_distance = 10;
	state->voice.common.input.far_distance = 1000;
	state->voice.common.input.position[2] = 20;
	state->frame.listener.elapsed = 1;
	state->frame.listener.position[2] = 10;
	state->frame.listener.front[2] = 1;
	state->frame.listener.up[1] = 1;
	state->frame.anchor = state->frame.listener.position;
	state->frame.secondary_bound = 1;
	state->frame.master_gain = 1;
	state->frame.parameters.gain = 1;
	state->frame.clock = &state->clock;
	CHECK(mh_clock_source_init(&state->clock, &clock_ops, state, NULL) ==
		MH_ERROR_NONE);
	CHECK(PCM_INIT(8000, 1, &state->output, NULL) == MH_ERROR_NONE);
	CHECK(PCM_BIND(&state->output, 0, &state->voice, &state->wave, NULL) ==
		MH_ERROR_NONE);
	PCM_SINK(&state->output, &state->sink);

	return 0;
}

static int
render_step(void)
{
	struct fixture *state;

	state = &fixture;
	QUEUE_STEP(&state->queue, &state->frame, &state->sink);
	state->now += (unsigned long)(state->frame.listener.elapsed * 1000);
	CHECK(PCM_RENDER(&state->output, state->pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static double
channel_energy(unsigned int channel)
{
	double sum;
	unsigned int frame_index;

	sum = 0;

	for (frame_index = 0; frame_index < FRAMES; frame_index++)
		sum += fixture.pcm[frame_index * 2 + channel] *
			fixture.pcm[frame_index * 2 + channel];

	return sum;
}

static unsigned int
crossings(void)
{
	unsigned int frame_index, count;

	count = 0;

	for (frame_index = 1; frame_index < FRAMES; frame_index++)
		if (fixture.pcm[(frame_index - 1) * 2] <= 0 &&
			fixture.pcm[frame_index * 2] > 0)
			count++;

	return count;
}

static int
distance_and_direction(void)
{
	struct fixture *state;
	double near_energy, far_energy;
	unsigned int sample_index;

	CHECK(setup_fixture(1, 1) == 0);
	state = &fixture;
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	near_energy = channel_energy(0);
	CHECK(near_energy > 1 &&
		fabs(near_energy - channel_energy(1)) < .00001);
	CHECK(crossings() >= 49 && crossings() <= 51);

	/* Distance doubles; let the source velocity settle first. */
	state->voice.common.input.position[2] = 30;
	CHECK(render_step() == 0);

	/*
	 * Restart at the same sample phase for an exact amplitude comparison.
	 */
	QUEUE_CLEAR(&state->queue, &state->sink);
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	far_energy = channel_energy(0);
	CHECK(fabs(far_energy / near_energy - .25) < .001);

	/* Source right, then rotate the listener to face the opposite way. */
	state->voice.common.input.position[0] = 20;
	state->voice.common.input.position[2] = 10;
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) > 1);
	state->frame.listener.front[2] = -1;
	CHECK(render_step() == 0);
	CHECK(channel_energy(1) == 0 && channel_energy(0) > 1);

	/*
	 * Queue admission uses its own .8 distance scale; PCM still mutes at
	 * max.
	 */
	state->voice.common.input.position[0] = 1000;
	CHECK(render_step() == 0);
	CHECK(state->voice.common.playback.active && channel_energy(0) == 0 &&
		channel_energy(1) == 0);

	for (sample_index = 0; sample_index < FRAMES * 2; sample_index++)
		CHECK(state->pcm[sample_index] == 0);

	return 0;
}

static int
motion(void)
{
	struct fixture *state;
	unsigned int stationary, approaching, receding;

	CHECK(setup_fixture(1, 1) == 0);
	state = &fixture;
	state->voice.common.input.position[2] = 210;
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	stationary = crossings();
	state->voice.common.input.position[2] = 110;
	CHECK(render_step() == 0);
	approaching = crossings();
	state->voice.common.input.position[2] = 210;
	CHECK(render_step() == 0);
	receding = crossings();
	CHECK(stationary >= 49 && stationary <= 51);
	CHECK(approaching >= 69 && approaching <= 72);
	CHECK(receding >= 37 && receding <= 40);

	/*
	 * Equal source/listener motion cancels; perpendicular motion has no
	 * shift.
	 */
	state->voice.common.input.position[2] += 100;
	state->frame.listener.position[2] += 100;
	CHECK(render_step() == 0);
	CHECK(crossings() >= 49 && crossings() <= 51);
	state->voice.common.input.position[0] = 10;
	state->frame.listener.position[0] = 10;
	CHECK(render_step() == 0);
	CHECK(crossings() >= 49 && crossings() <= 51);

	/* Listener alone moves toward the now-stationary source. */
	state->frame.listener.position[2] += 50;
	CHECK(render_step() == 0);
	CHECK(crossings() >= 56 && crossings() <= 59);

	return 0;
}

static int
lifetime(void)
{
	struct fixture *state;
	struct mh_spatial_playback_status status;
	double cursor;

	CHECK(setup_fixture(0, 1) == 0);
	state = &fixture;
	state->voice.common.input.pan = 1;
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) > 1);
	CHECK(crossings() == 0);
	cursor = state->output.output.mixer.voices[0].cursor;
	CHECK(PCM_RENDER(&state->output, state->pcm, FRAMES, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);
	CHECK(state->output.output.mixer.voices[0].cursor == cursor);
	QUEUE_CLEAR(&state->queue, &state->sink);
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);
	CHECK(!state->output.output.mixer.voices[0].active);

	/*
	 * End-of-sample status must remove a completed one-shot from the queue.
	 */
	CHECK(setup_fixture(1, 0) == 0);
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	memset(&status, 0, sizeof(status));
	state->sink.status(state->sink.context, &state->voice, &status);
	CHECK(!status.playing && !status.looping);
	CHECK(render_step() == 0);
	CHECK(!state->voice.common.playback.active &&
		state->voice.common.slot == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);

	return 0;
}

static int
stereo_buffer(void)
{
	static unsigned char stereo[32];
	struct fixture *state;
	unsigned int byte_index;

	CHECK(setup_fixture(0, 1) == 0);
	state = &fixture;

	for (byte_index = 0; byte_index < sizeof(stereo); byte_index += 2) {
		stereo[byte_index] = 200;
		stereo[byte_index + 1] = 56;
	}

	state->wave.pcm = stereo;
	state->wave.bytes = sizeof(stereo);
	state->wave.channels = 2;
	CHECK(PCM_BIND(&state->output, 0, &state->voice, &state->wave, NULL) ==
		MH_ERROR_NONE);
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	CHECK(state->pcm[0] > 0 && state->pcm[1] < 0);
	QUEUE_CLEAR(&state->queue, &state->sink);

#ifdef MH_TEST_A3D12
	/*
	 * Capabilities describe native CPU placement, not requested hardware.
	 */
	CHECK(setup_fixture(1, 1) == 0);
	state->voice.hardware = 1;
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0 && state->voice.hardware == 0);
	QUEUE_CLEAR(&state->queue, &state->sink);
#endif

	return 0;
}

static int
levels(void)
{
	struct fixture *state;
	struct mh_spatial_playback_request request;
	double full;

	CHECK(setup_fixture(0, 1) == 0);
	state = &fixture;
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(render_step() == 0);
	full = channel_energy(0);
	CHECK(full > 1 && fabs(full - channel_energy(1)) < .00001);
	memset(&request, 0, sizeof(request));
	request.action = MH_SPATIAL_PLAYBACK_UPDATE;
	request.frequency = 8000;
	request.volume = -2000;
	state->sink.playback(state->sink.context, &state->voice, &request);
	CHECK(PCM_RENDER(&state->output, state->pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(channel_energy(0) / full - .01) < .00001);
	request.volume = -10000;
	state->sink.playback(state->sink.context, &state->voice, &request);
	CHECK(PCM_RENDER(&state->output, state->pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);
	CHECK(state->output.output.mixer.voices[0].active);

	return 0;
}

static int
replacement(void)
{
	struct fixture *state;
	const struct mh_mixer *mixer;
	static struct VOICE voices[VOICE_COUNT + 1];
	unsigned int voice_index, active;

	CHECK(setup_fixture(1, 1) == 0);
	state = &fixture;

	for (voice_index = 0; voice_index <= VOICE_COUNT; voice_index++) {
		voices[voice_index] = state->voice;
		voices[voice_index].common.input.priority =
			voice_index == VOICE_COUNT ? 100 : 1;
		CHECK(PCM_BIND(&state->output, voice_index,
			&voices[voice_index], &state->wave,
			NULL) == MH_ERROR_NONE);
		QUEUE_REQUEST(
			&state->queue, &voices[voice_index], &state->sink);
	}

	CHECK(render_step() == 0);
	active = 0;
	mixer = &state->output.output.mixer;

	for (voice_index = 0; voice_index <= VOICE_COUNT; voice_index++)
		active += mixer->voices[voice_index].active != 0;

	CHECK(active == VOICE_COUNT);
	CHECK(state->output.output.voices[0].channel == 0);
	CHECK(state->output.output.voices[VOICE_COUNT].channel != 0);
	QUEUE_CLEAR(&state->queue, &state->sink);
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);

	return 0;
}

static int
errors(void)
{
	struct fixture *state;
	struct VOICE unbound;
	struct mh_spatial_source_update update;
	struct mh_error error;

	CHECK(setup_fixture(1, 1) == 0);
	state = &fixture;
	CHECK(PCM_BIND(&state->output, 1, &state->voice, &state->wave, NULL) ==
		MH_ERROR_EXISTS);
	state->wave.channels = 2;
	state->wave.frames /= 2;
	CHECK(PCM_BIND(&state->output, 0, &state->voice, &state->wave, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(setup_fixture(1, 1) == 0);
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	CHECK(PCM_BIND(&state->output, 0, &state->voice, &state->wave, NULL) ==
		MH_ERROR_STATE);
	CHECK(render_step() == 0);
	memset(&unbound, 0, sizeof(unbound));
	memset(&update, 0, sizeof(update));
	update.changed = 1;
	state->sink.source(state->sink.context, &unbound, &update);
	CHECK(PCM_RENDER(&state->output, state->pcm, FRAMES, 0, &error) ==
		MH_ERROR_STATE);
	CHECK(error.message != NULL);

	/*
	 * Even after a callback failure, teardown must stop existing PCM
	 * voices.
	 */
	QUEUE_CLEAR(&state->queue, &state->sink);
	CHECK(!state->output.output.mixer.voices[0].active);

	return 0;
}

static int
transitions(void)
{
	static struct PCM partitioned;
	static unsigned char flat[16];
	struct fixture *state;
	struct mh_mixer_voice *voice;
	float whole[80], chunks[80];
	double level, cursor;
	unsigned int frame_index, sample_index;

	CHECK(setup_fixture(0, 1) == 0);
	state = &fixture;
	memset(flat, 192, sizeof(flat));
	state->wave.pcm = flat;
	state->output.output.transition_frames =
		40; /* Five milliseconds at 8 kHz. */
	QUEUE_REQUEST(&state->queue, &state->voice, &state->sink);
	QUEUE_STEP(&state->queue, &state->frame, &state->sink);
	partitioned = state->output;
	CHECK(PCM_RENDER(&state->output, whole, 40, 0, NULL) == MH_ERROR_NONE);
	CHECK(PCM_RENDER(&partitioned, chunks, 7, 0, NULL) == MH_ERROR_NONE);
	CHECK(PCM_RENDER(&partitioned, chunks + 14, 13, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(PCM_RENDER(&partitioned, chunks + 40, 20, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(whole, chunks, sizeof(whole)) == 0);
	level = whole[78];
	CHECK(level > .1 && fabs(whole[0] - level / 40) < .000001);

	for (frame_index = 1; frame_index < 40; frame_index++)
		CHECK(whole[frame_index * 2] > whole[(frame_index - 1) * 2] &&
			fabs(whole[frame_index * 2] -
				whole[(frame_index - 1) * 2] - level / 40) <
				.000001);

	/* Bus volume uses the same ramp, and pause freezes its progress. */
	state->output.output.gain = 0;
	CHECK(PCM_RENDER(&state->output, whole, 10, 0, NULL) == MH_ERROR_NONE);
	voice = &state->output.output.mixer.voices[0];
	CHECK(voice->transition == 30);
	cursor = voice->cursor;
	CHECK(PCM_RENDER(&state->output, chunks, 40, 1, NULL) == MH_ERROR_NONE);
	CHECK(voice->transition == 30 && voice->cursor == cursor);
	CHECK(PCM_RENDER(&state->output, whole + 20, 30, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(whole[78] == 0 && voice->transition == 0);

	for (frame_index = 1; frame_index < 40; frame_index++)
		CHECK(whole[frame_index * 2] < whole[(frame_index - 1) * 2]);

	/*
	 * New pitch targets settle at a fixed sample count, not callback count.
	 */
	state->output.output.gain = 1;
	state->output.output.voices[0].frequency = 16000;
	CHECK(PCM_RENDER(&state->output, whole, 20, 0, NULL) == MH_ERROR_NONE);
	CHECK(fabs(voice->pitch - 1.5) < .000001 && voice->transition == 20);
	CHECK(PCM_RENDER(&state->output, whole, 20, 0, NULL) == MH_ERROR_NONE);
	CHECK(voice->pitch == 2 && voice->transition == 0);
	QUEUE_CLEAR(&state->queue, &state->sink);
	CHECK(PCM_RENDER(&state->output, whole, 40, 0, NULL) == MH_ERROR_NONE);

	for (sample_index = 0; sample_index < 80; sample_index++)
		CHECK(whole[sample_index] == 0);

	return 0;
}

int
main(void)
{

	if (distance_and_direction() || motion() || lifetime() || levels() ||
		replacement() || errors() || transitions() || stereo_buffer())
		return 1;

	puts("Spatial queue to PCM: distance, direction, Doppler, pause and lifetime verified");

	return 0;
}
