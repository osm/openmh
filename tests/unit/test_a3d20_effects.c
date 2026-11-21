#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_effects.h"
#include "engine/audio/mixer_internal.h"
#include "interfaces/clock/backend.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "spatial effects line %d: %s\n",       \
				__LINE__, #test);                              \
			return 1;                                              \
		}                                                              \
	} while (0)

#define BANKS 4
#define FRAMES 800

static struct mh_a3d20_effects effects;
static struct mh_sound_pool pool;
static struct mh_mixer mixers[BANKS];
static struct mh_mixer *banks[BANKS];
static unsigned char tone[FRAMES];
static float pcm[FRAMES * 2];
static unsigned long now;
static const char *sample_path = "plain.wav";

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = now;
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
	(void)context;

	return now;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static const char *
sample_name(void *context, const struct mh_audio_pcm *wave)
{
	(void)context;
	(void)wave;

	return sample_path;
}

static int
render_step(void)
{
	struct mh_error error;

	now += 100;

	if (mh_a3d20_effects_update(&effects, &pool, banks, BANKS, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(mh_a3d20_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static double
channel_energy(unsigned int channel)
{
	unsigned int frame;
	double sum;

	sum = 0;

	for (frame = 0; frame < FRAMES; frame++)
		sum += pcm[frame * 2 + channel] * pcm[frame * 2 + channel];

	return sum;
}

int
main(void)
{
	struct mh_clock_source clock;
	struct mh_audio_pcm wave;
	struct mh_sound_listener listener;
	unsigned int i, active, record_index, stopped;
	double position[3], plain_energy;

	now = 1000;
	memset(&wave, 0, sizeof(wave));
	memset(&listener, 0, sizeof(listener));
	memset(tone, 200, sizeof(tone));
	wave.pcm = tone;
	wave.frames = wave.bytes = sizeof(tone);
	wave.channels = 1;
	wave.bits = 8;
	wave.rate = 8000;
	listener.forward[2] = listener.right[0] = 1;
	listener.position[2] = 10;
	position[0] = 20;
	position[1] = -.5;
	position[2] = 10;
	CHECK(mh_clock_source_init(&clock, &clock_ops, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_a3d20_effects_init(8000, .125, &clock, NULL, sample_name, NULL,
		&effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_init_channels(BANKS * MH_MIXER_VOICES,
		MH_A3D20_VOICES, &pool, NULL) == MH_ERROR_NONE);

	for (i = 0; i < BANKS; i++) {
		banks[i] = &mixers[i];
		CHECK(mh_mixer_init(8000, .125, banks[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_bind(banks[i], &pool, i * MH_MIXER_VOICES,
			NULL) == MH_ERROR_NONE);
	}

	/* A late-bank emitter must not be constrained by the PCM slot count. */
	record_index = 3 * MH_MIXER_VOICES;
	mh_a3d20_effects_listener(&effects, &listener);
	CHECK(mh_mixer_policy(banks[3], 0, MH_SOUND_POSITIONAL | MH_SOUND_LOOP,
		1, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_spatial_bind(banks[3], 0, &listener, position, 1, 10,
		100, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[3], 0, &wave, 1, .1, .5, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	banks[3]->fade_in[0] = banks[3]->fade_out[0] = 100;
	CHECK(render_step() == 0);
	CHECK(effects.waves[record_index].pcm == wave.pcm + 100 &&
		effects.waves[record_index].frames == 600);
	CHECK(effects.voices[record_index].fade_in_samples == 100 &&
		effects.voices[record_index].fade_out_samples == 100);
	CHECK(render_step() == 0);
	CHECK(channel_energy(1) > 0 && channel_energy(0) == 0);
	CHECK(banks[3]->voices[0].active &&
		pool.records[record_index].channel != 0);

	/* Explicit stops release channels; sample payloads remain cached. */
	mh_mixer_stop(banks[3], 0);
	mh_a3d20_effects_stops(&effects, &pool);
	CHECK(effects.pcm.output.voices[record_index].channel == 0);
	CHECK(mh_a3d20_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);
	banks[3]->fade_in[0] = banks[3]->fade_out[0] = 0;
	CHECK(mh_mixer_policy(banks[3], 0, 0, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[3], 0, &wave, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(channel_energy(0) > 0 && channel_energy(0) == channel_energy(1));

	/*
	 * Admission precedes completion: do not recycle the old PCM binding
	 * early.
	 */
	CHECK(mh_mixer_policy(banks[0], 1, MH_SOUND_LOOP, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 1, &wave, 1, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(banks[0]->voices[1].active && channel_energy(0) > 0);
	mh_mixer_stop(banks[0], 1);
	mh_a3d20_effects_stops(&effects, &pool);
	CHECK(!banks[3]->voices[0].active &&
		!pool.records[record_index].active);

	/* All twenty-four resident slots are visible to producer clear/stop. */
	for (i = 0; i < MH_A3D20_VOICES; i++) {
		CHECK(mh_mixer_policy(banks[i % BANKS], i, MH_SOUND_LOOP, 1,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[i % BANKS], i, &wave, 1, 1, 1, 0,
			wave.frames, NULL) == MH_ERROR_NONE);
	}

	CHECK(render_step() == 0);
	active = 0;

	for (i = 0; i < MH_A3D20_VOICES; i++)
		active += pool.channels[i] != 0;

	CHECK(active == MH_A3D20_VOICES);
	CHECK(mh_sound_pool_clear(&pool, &stopped, NULL) == MH_ERROR_NONE);
	CHECK(stopped == (1U << MH_A3D20_VOICES) - 1);
	mh_a3d20_effects_stops(&effects, &pool);
	CHECK(render_step() == 0 && channel_energy(0) == 0 &&
		channel_energy(1) == 0);

	/*
	 * Restart consumes one request and resets the physical sample cursor.
	 */
	CHECK(mh_mixer_play(banks[0], 0, &wave, .5, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(mh_mixer_play(banks[0], 0, &wave, .25, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_source_levels(banks[0], 0, 2, .5, NULL) ==
		MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(effects.voices[0].input.gain == 2 &&
		effects.voices[0].input.pan == .5f);
	CHECK(banks[0]->voices[0].left == 1 && banks[0]->voices[0].right == 1);
	CHECK(effects.pcm.output.mixer
			.voices[effects.pcm.output.voices[0].channel - 1]
			.cursor == 200);
	mh_a3d20_effects_clear(&effects);
	CHECK(mh_a3d20_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);

	/* The bridge preserves the actual filename for gain tuning. */
	CHECK(mh_mixer_play(banks[0], 0, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0);
	plain_energy = channel_energy(0);
	CHECK(plain_energy > 0);
	mh_a3d20_effects_clear(&effects);
	sample_path = "gason.wav";
	CHECK(mh_mixer_play(banks[0], 0, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(strcmp(effects.voices[0].name, "gason.wav") == 0);
	CHECK(channel_energy(0) > plain_energy * 2.55 &&
		channel_energy(0) < plain_energy * 2.57);
	mh_a3d20_effects_clear(&effects);
	sample_path = "plain.wav";

	/*
	 * Duplicates retain the first trimmed payload, but use each new fade.
	 */
	CHECK(effects.sample_count == 2);
	CHECK(mh_mixer_play(banks[0], 2, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	banks[0]->fade_in[2] = 200;
	CHECK(render_step() == 0);
	CHECK(effects.waves[2].pcm == wave.pcm + 100 &&
		effects.waves[2].frames == 600);
	CHECK(effects.voices[2].fade_in_samples == 200 &&
		effects.sample_count == 2);
	CHECK(effects.voices[2].input.near_distance == 1 &&
		effects.voices[2].input.far_distance > 1);
	mh_a3d20_effects_clear(&effects);
	CHECK(mh_a3d20_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(channel_energy(0) == 0 && channel_energy(1) == 0);

	/* Full names and case remain distinct cache keys. */
	sample_path = "other/plain.wav";
	CHECK(mh_mixer_play(banks[0], 2, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0);
	CHECK(effects.sample_count == 3 &&
		effects.waves[2].pcm == wave.pcm + 200);
	mh_a3d20_effects_clear(&effects);
	sample_path = "Plain.wav";
	CHECK(mh_mixer_play(banks[0], 2, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(render_step() == 0 && effects.sample_count == 4);
	mh_a3d20_effects_clear(&effects);
	mh_a3d20_pcm_destroy(&effects.pcm);
	puts("A3D 2.0 producer banks: 24 voices, relative sources, exact-name payload reuse, fades and lifecycle pass");

	return 0;
}
