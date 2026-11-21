#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_effects.h"
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

static struct mh_eax_effects effects;
static struct mh_sound_pool pool;
static struct mh_mixer bank_storage[BANKS];
static struct mh_mixer *banks[BANKS];
static unsigned char tone[FRAMES];
static float pcm[FRAMES * 2];
static unsigned long clock_ms;
static const char *current_sample_name = "plain.wav";

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = clock_ms;
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

	return clock_ms;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static const char *
sample_name(void *context, const struct mh_audio_pcm *wave)
{
	(void)context;
	(void)wave;

	return current_sample_name;
}

static int
step(void)
{
	struct mh_error error;

	clock_ms += 100;

	if (mh_eax_effects_update(&effects, &pool, banks, BANKS, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static double
energy(unsigned int channel)
{
	unsigned int frame_index;
	double sum;

	sum = 0;

	for (frame_index = 0; frame_index < FRAMES; frame_index++)
		sum += pcm[frame_index * 2 + channel] *
			pcm[frame_index * 2 + channel];

	return sum;
}

int
main(void)
{
	struct mh_clock_source clock;
	struct mh_audio_pcm wave;
	struct mh_sound_listener listener;
	const struct mh_mixer_voice *physical_voice;
	unsigned int bank_index, voice_index, slot_index, active, record_index,
		stopped, channel;
	double position[3], plain_energy;

	clock_ms = 1000;
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
	position[1] = 0;
	position[2] = 10;
	CHECK(mh_clock_source_init(&clock, &clock_ops, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_eax_effects_init(8000, .125, &clock, sample_name, NULL,
		&effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_init_channels(BANKS * MH_MIXER_VOICES,
		MH_EAX_VOICES, &pool, NULL) == MH_ERROR_NONE);

	for (bank_index = 0; bank_index < BANKS; bank_index++) {
		banks[bank_index] = &bank_storage[bank_index];
		CHECK(mh_mixer_init(8000, .125, banks[bank_index], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_bind(banks[bank_index], &pool,
			bank_index * MH_MIXER_VOICES,
			NULL) == MH_ERROR_NONE);
	}

	/* A late-bank emitter must not be constrained by the PCM slot count. */
	record_index = 3 * MH_MIXER_VOICES;
	mh_eax_effects_listener(&effects, &listener, 0);
	CHECK(mh_mixer_policy(banks[3], 0, MH_SOUND_POSITIONAL | MH_SOUND_LOOP,
		1, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_spatial_bind(banks[3], 0, &listener, position, 1, 10,
		100, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[3], 0, &wave, 1, .1, .5, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	banks[3]->fade_in[0] = banks[3]->fade_out[0] = 100;
	CHECK(step() == 0);
	CHECK(effects.waves[record_index].pcm == wave.pcm + 100 &&
		effects.waves[record_index].frames == 600);
	CHECK(effects.voices[record_index].fade_in_samples == 100 &&
		effects.voices[record_index].fade_out_samples == 100);
	CHECK(step() == 0);
	CHECK(energy(1) > 0 && energy(0) == 0);
	CHECK(banks[3]->voices[0].active &&
		pool.records[record_index].channel != 0);

	/* Explicit stops and immediate rebind retain no old sample/channel. */
	mh_mixer_stop(banks[3], 0);
	mh_eax_effects_stops(&effects, &pool);
	CHECK(effects.pcm.output.voices[record_index].channel == 0);
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	banks[3]->fade_in[0] = banks[3]->fade_out[0] = 0;
	CHECK(mh_mixer_policy(banks[3], 0, 0, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[3], 0, &wave, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(energy(0) > 0 && energy(0) == energy(1));

	/*
	 * Admission precedes completion: do not recycle the old PCM binding
	 * early.
	 */
	CHECK(mh_mixer_policy(banks[0], 1, MH_SOUND_LOOP, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 1, &wave, 1, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(banks[0]->voices[1].active && energy(0) > 0);
	mh_mixer_stop(banks[0], 1);
	mh_eax_effects_stops(&effects, &pool);
	CHECK(!banks[3]->voices[0].active &&
		!pool.records[record_index].active);

	/* All twenty-four resident slots are visible to producer clear/stop. */
	for (voice_index = 0; voice_index < MH_EAX_VOICES; voice_index++) {
		CHECK(mh_mixer_policy(banks[voice_index % BANKS], voice_index,
			MH_SOUND_LOOP, 1, NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[voice_index % BANKS], voice_index,
			&wave, 1, 1, 1, 0, wave.frames,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(step() == 0);
	active = 0;

	for (slot_index = 0; slot_index < MH_EAX_VOICES; slot_index++)
		active += pool.channels[slot_index] != 0;

	CHECK(active == MH_EAX_VOICES);
	CHECK(mh_sound_pool_clear(&pool, &stopped, NULL) == MH_ERROR_NONE);
	CHECK(stopped == (1U << MH_EAX_VOICES) - 1);
	mh_eax_effects_stops(&effects, &pool);
	CHECK(step() == 0 && energy(0) == 0 && energy(1) == 0);

	/*
	 * Restart consumes one request and resets the physical sample cursor.
	 */
	CHECK(mh_mixer_play(banks[0], 0, &wave, .5, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(mh_mixer_play(banks[0], 0, &wave, .25, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_source_levels(banks[0], 0, 2, .5, NULL) ==
		MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(effects.voices[0].input.gain == 2 &&
		effects.voices[0].input.pan == .5f);
	CHECK(banks[0]->voices[0].left == 1 && banks[0]->voices[0].right == 1);
	channel = effects.pcm.output.voices[0].channel;
	physical_voice = &effects.pcm.output.mixer.voices[channel - 1];
	CHECK(physical_voice->cursor == 200);
	mh_eax_effects_clear(&effects);
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);

	/* The bridge preserves the actual filename for gain tuning. */
	CHECK(mh_mixer_play(banks[0], 0, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(step() == 0);
	plain_energy = energy(0);
	CHECK(plain_energy > 0);
	mh_eax_effects_clear(&effects);
	current_sample_name = "gason.wav";
	CHECK(mh_mixer_play(banks[0], 0, &wave, 1, .05, .05, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(strcmp(effects.voices[0].name, "gason.wav") == 0);
	CHECK(energy(0) > plain_energy * 400 && energy(0) < plain_energy * 430);
	mh_eax_effects_clear(&effects);
	current_sample_name = "plain.wav";

	/*
	 * Reverb survives a source stop, pauses with PCM, and clears on reset.
	 */
	mh_eax_effects_listener(&effects, &listener, 1);
	CHECK(mh_mixer_play(banks[0], 0, &wave, 1, 1, 1, 0, wave.frames,
		NULL) == MH_ERROR_NONE);
	CHECK(step() == 0);
	CHECK(effects.listener_state.reverb > .349f &&
		effects.listener_state.reverb < .351f);
	mh_mixer_stop(banks[0], 0);
	mh_eax_effects_stops(&effects, &pool);
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) > 0 && energy(1) > 0);
	effects.pcm.volume = 0;
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	effects.pcm.volume = 1;
	mh_eax_effects_clear(&effects);
	CHECK(mh_eax_pcm_render(&effects.pcm, pcm, FRAMES, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	mh_eax_pcm_destroy(&effects.pcm);
	puts("EAX producer banks: 24 voices, relative sources, trims, completion, pause, mute and reverb reset pass");

	return 0;
}
