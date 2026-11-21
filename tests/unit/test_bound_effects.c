#include <stdio.h>
#include <string.h>

#include "engine/audio/mixer_internal.h"
/* Producer-bank scenarios shared by the two bound-source profiles. */
#ifdef MH_TEST_A3D12
#include "engine/audio/a3d12_effects.h"
#define EFFECTS mh_a3d12_effects
#define VOICE_COUNT MH_A3D12_VOICES
#define PCM_RENDER mh_a3d12_pcm_render
#define EFFECTS_INIT mh_a3d12_effects_init
#define EFFECTS_UPDATE mh_a3d12_effects_update
#define EFFECTS_LISTENER mh_a3d12_effects_listener
#define EFFECTS_STOPS mh_a3d12_effects_stops
#define EFFECTS_CLEAR mh_a3d12_effects_clear
#else
#include "engine/audio/ds3d_effects.h"
#define EFFECTS mh_ds3d_effects
#define VOICE_COUNT MH_DS3D_VOICES
#define PCM_RENDER mh_ds3d_pcm_render
#define EFFECTS_INIT mh_ds3d_effects_init
#define EFFECTS_UPDATE mh_ds3d_effects_update
#define EFFECTS_LISTENER mh_ds3d_effects_listener
#define EFFECTS_STOPS mh_ds3d_effects_stops
#define EFFECTS_CLEAR mh_ds3d_effects_clear
#endif
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

static struct EFFECTS effects;
static struct mh_sound_pool pool;
static struct mh_mixer storage[BANKS];
static struct mh_mixer *banks[BANKS];
static unsigned char tone[FRAMES];
static float pcm[FRAMES * 2];
static unsigned long now;

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

static int
step(void)
{
	struct mh_error error;

	now += 100;

	if (EFFECTS_UPDATE(&effects, &pool, banks, BANKS, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(PCM_RENDER(&effects.pcm, pcm, FRAMES, 0, NULL) == MH_ERROR_NONE);

	return 0;
}

static double
energy(unsigned int channel)
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
	const struct mh_mixer_voice *pcm_voice;
	unsigned int bank_index, voice_index, slot, active, record_index,
		stopped;
	double position[3];

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
	position[1] = 0;
	position[2] = 10;
	CHECK(mh_clock_source_init(&clock, &clock_ops, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(EFFECTS_INIT(8000, .125, &clock, &effects, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_sound_pool_init_channels(BANKS * MH_MIXER_VOICES, VOICE_COUNT,
		&pool, NULL) == MH_ERROR_NONE);

	for (bank_index = 0; bank_index < BANKS; bank_index++) {
		banks[bank_index] = &storage[bank_index];
		CHECK(mh_mixer_init(8000, .125, banks[bank_index], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_bind(banks[bank_index], &pool,
			bank_index * MH_MIXER_VOICES,
			NULL) == MH_ERROR_NONE);
	}

	/* Reject bad bank ownership before advancing service state. */
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, 0, NULL) ==
		MH_ERROR_INVALID);
	banks[1] = NULL;
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, BANKS, NULL) ==
		MH_ERROR_INVALID);
	banks[1] = &storage[1];
	banks[1]->record_base++;
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, BANKS, NULL) ==
		MH_ERROR_INVALID);
	banks[1]->record_base--;
	CHECK(!effects.serviced);
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.frame.listener.elapsed == .04f &&
		effects.frame.anchor == NULL);
	now += 250;
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.frame.listener.elapsed == .25f);
	effects.frame.parameters.gain = 123;
	EFFECTS_LISTENER(&effects, &listener);
	CHECK(EFFECTS_UPDATE(&effects, &pool, banks, BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.frame.listener.elapsed == .04f &&
		effects.frame.parameters.gain == 0);
	CHECK(effects.frame.anchor == effects.frame.listener.position &&
		effects.frame.secondary_bound);
	CHECK(effects.frame.listener.front[2] == 1 &&
		effects.frame.listener.up[1] == 1);
	now = 1000;
	CHECK(EFFECTS_INIT(8000, .125, &clock, &effects, NULL) ==
		MH_ERROR_NONE);

	/* A late-bank emitter must not be constrained by the PCM slot count. */
	record_index = 3 * MH_MIXER_VOICES;
	EFFECTS_LISTENER(&effects, &listener);
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
	CHECK(effects.voices[record_index].common.fade_in_samples == 100 &&
		effects.voices[record_index].common.fade_out_samples == 100);
	CHECK(step() == 0);
	CHECK(energy(1) > 0 && energy(0) == 0);
	CHECK(banks[3]->voices[0].active &&
		pool.records[record_index].channel != 0);

	/* Explicit stops and immediate rebind retain no old sample/channel. */
	mh_mixer_stop(banks[3], 0);
	EFFECTS_STOPS(&effects, &pool);
	CHECK(effects.pcm.output.voices[record_index].channel == 0);
	CHECK(PCM_RENDER(&effects.pcm, pcm, FRAMES, 0, NULL) == MH_ERROR_NONE);
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
	EFFECTS_STOPS(&effects, &pool);
	CHECK(!banks[3]->voices[0].active &&
		!pool.records[record_index].active);

	/* All sixteen resident slots are visible to producer clear/stop. */
	for (voice_index = 0; voice_index < VOICE_COUNT; voice_index++) {
		CHECK(mh_mixer_policy(banks[voice_index % BANKS], voice_index,
			MH_SOUND_LOOP, 1, NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[voice_index % BANKS], voice_index,
			&wave, 1, 1, 1, 0, wave.frames,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(step() == 0);
	active = 0;

	for (slot = 0; slot < VOICE_COUNT; slot++)
		active += pool.channels[slot] != 0;

	CHECK(active == VOICE_COUNT);
	CHECK(mh_sound_pool_clear(&pool, &stopped, NULL) == MH_ERROR_NONE);
	CHECK(stopped == (1U << VOICE_COUNT) - 1);
	EFFECTS_STOPS(&effects, &pool);
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
	CHECK(effects.voices[0].common.input.gain == 2 &&
		effects.voices[0].common.input.pan == .5f);
	CHECK(banks[0]->voices[0].left == 1 && banks[0]->voices[0].right == 1);
	pcm_voice = &effects.pcm.output.mixer
			.voices[effects.pcm.output.voices[0].channel - 1];
	CHECK(pcm_voice->cursor == 200);
	EFFECTS_CLEAR(&effects);
	CHECK(PCM_RENDER(&effects.pcm, pcm, FRAMES, 0, NULL) == MH_ERROR_NONE);
	CHECK(energy(0) == 0 && energy(1) == 0);
	puts("Spatial producer banks: trims, channel ownership, completion, clear and restart pass");

	return 0;
}
