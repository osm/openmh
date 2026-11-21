#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/ambient.h"
#include "engine/audio/effects.h"
#include "engine/audio/race_sound.h"
#include "engine/audio/surface.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define TEST_BANKS 3
#define SAMPLE_FRAMES 256
#define SAMPLE_BYTES (SAMPLE_FRAMES * 2)

enum alias_scenario {
	ACTIVE_ALIASES,
	INACTIVE_PENDING,
	INACTIVE_ALIASES,
	UNREACHABLE_ACTIVE,
	CROSS_LINKED_CHANNELS,
	ALIAS_SCENARIOS
};

struct alias_expectation {
	unsigned int	active_records, pending_records, physical_mask;
};

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
constant_wave(struct mh_audio_pcm *sample, unsigned char pcm[SAMPLE_BYTES])
{
	unsigned int i;

	for (i = 0; i < SAMPLE_FRAMES; i++) {
		pcm[i * 2] = 0;
		pcm[i * 2 + 1] = 64;
	}

	memset(sample, 0, sizeof(*sample));
	sample->pcm = pcm;
	sample->frames = SAMPLE_FRAMES;
	sample->bytes = SAMPLE_BYTES;
	sample->rate = 22050;
	sample->bits = 16;
	sample->channels = 1;
}

static void
bind_banks(struct mh_sound_pool *pool, struct mh_effects_mixer *effects,
	struct mh_mixer mixers[TEST_BANKS], struct mh_mixer *banks[TEST_BANKS])
{
	unsigned int i;

	CHECK(mh_sound_pool_init(TEST_BANKS * MH_MIXER_VOICES, pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_init(22050, .125, effects, NULL) == MH_ERROR_NONE);

	for (i = 0; i < TEST_BANKS; i++) {
		CHECK(mh_mixer_init(22050, .125, &mixers[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_bind(&mixers[i], pool, i * MH_MIXER_VOICES,
			NULL) == MH_ERROR_NONE);
		banks[i] = &mixers[i];
	}
}

static void
channel_admission(void)
{
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, saved_effects;
	struct mh_mixer mixers[TEST_BANKS], saved_mixers[TEST_BANKS],
		*banks[TEST_BANKS];
	struct mh_audio_pcm sample, missing;
	unsigned char pcm[SAMPLE_BYTES];
	float output[1024];
	unsigned int i;

	constant_wave(&sample, pcm);
	bind_banks(&pool, &effects, mixers, banks);

	for (i = 0; i < 8; i++) {
		CHECK(mh_mixer_policy(banks[0], i, 1, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], i, &sample, 1, 1, 1, 0, 256,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_mixer_render(banks[0], output, 1, 0, NULL) !=
		MH_ERROR_NONE); /* No bypass of pool admission. */
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 2,
		0, NULL) == MH_ERROR_NONE);
	CHECK(output[0] == .5f && output[3] == .5f &&
		banks[0]->voices[0].cursor == 2);
	CHECK(mh_mixer_policy(banks[1], 0, 1, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 256, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.records[0] == MH_MIXER_VOICES + 1 &&
		!banks[0]->voices[0].active && banks[1]->voices[0].active);
	CHECK(effects.pcm.voices[0].cursor == 0 &&
		effects.pcm.voices[1].cursor == 2);
	CHECK(mh_mixer_policy(banks[2], 0, 3, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[2], 0, &sample, 1, .5, .5, 0, 256, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		effects.records[1] == 2 * MH_MIXER_VOICES + 1);
	CHECK(!banks[0]->voices[1].active); /* Ambient competes in the same
					eight channels. */
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == .46875f);
	saved_pool = pool;
	saved_effects = effects;
	memcpy(saved_mixers, mixers, sizeof(mixers));
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 16,
		1, NULL) == MH_ERROR_NONE);
	CHECK(output[0] == 0 && output[31] == 0);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
	CHECK(memcmp(mixers, saved_mixers, sizeof(mixers)) == 0);

	/* A failed start and failed device update preserve all shared state. */
	missing = sample;
	missing.pcm = NULL;
	CHECK(mh_mixer_play(banks[1], 1, &missing, 1, 1, 1, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(mixers, saved_mixers, sizeof(mixers)) == 0);
	CHECK(mh_mixer_policy(banks[1], 1, 0, 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[1], 1, &sample, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	banks[1]->voices[1].wave = &missing;
	saved_pool = pool;
	memcpy(saved_mixers, mixers, sizeof(mixers));
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
	CHECK(memcmp(mixers, saved_mixers, sizeof(mixers)) == 0);
}

static void
loop_refresh(void)
{
	struct mh_sound_pool pool;
	struct mh_effects_mixer effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	float output[1024];

	constant_wave(&sample, pcm);

	/*
	 * Adjustments retain the original physical loop window and cursor,
	 * including changes to the same sample. Disabling looping releases its
	 * tail; only an explicit restart can install new bounds.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	CHECK(mh_mixer_policy(banks[1], 0, 1, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 2, 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 7,
		0, NULL) == MH_ERROR_NONE);
	CHECK(banks[1]->voices[0].cursor == 7);
	banks[1]->voices[0].loop_start = 2;
	banks[1]->voices[0].loop_end = 6;
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(banks[1]->voices[0].cursor == 7 &&
		effects.pcm.voices[0].cursor == 7);
	CHECK(effects.loop_start[0] == 2 && effects.loop_end[0] == 8);
	banks[1]->voices[0].loop_start = banks[1]->voices[0].loop_end = 0;
	CHECK(mh_mixer_policy(banks[1], 0, 0, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 5,
		0, NULL) == MH_ERROR_NONE);
	CHECK(banks[1]->voices[0].cursor == 12 &&
		effects.pcm.voices[0].loop_end == 0);
	banks[1]->voices[0].loop_start = 2;
	banks[1]->voices[0].loop_end = 6;
	CHECK(mh_mixer_policy(banks[1], 0, 1, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].cursor == 12 &&
		effects.pcm.voices[0].loop_end == 8);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].cursor == 7);
	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(banks[1]->voices[0].cursor == 0 &&
		effects.pcm.voices[0].cursor == 0);
}

static void
completion_admission(void)
{
	struct mh_sound_pool pool;
	struct mh_effects_mixer effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	float output[1024];
	unsigned int i;

	constant_wave(&sample, pcm);

	/*
	 * Physical completion remains logical-active until the next callback.
	 * Lower-priority requests lose BEFORE that callback releases residents.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	sample.frames = 4;

	for (i = 0; i < 8; i++) {
		CHECK(mh_mixer_policy(banks[0], i, 0, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], i, &sample, 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 4,
		0, NULL) == MH_ERROR_NONE);
	CHECK(!effects.pcm.voices[0].active && banks[0]->voices[0].active);
	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		!banks[1]->voices[0].active);

	for (i = 0; i < 8; i++)
		CHECK(pool.channels[i] == 0 && !banks[0]->voices[i].active);

	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		effects.records[0] == MH_MIXER_VOICES + 1);
}

static void
pending_limit(void)
{
	struct mh_sound_pool pool;
	struct mh_effects_mixer effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	float output[1024];
	unsigned int i, count;

	constant_wave(&sample, pcm);
	sample.frames = 4;

	/* Logical requests across banks obey the shared 32-pending limit. */
	bind_banks(&pool, &effects, mixers, banks);

	for (i = 0; i < 2 * MH_MIXER_VOICES; i++)
		CHECK(mh_mixer_play(banks[i / MH_MIXER_VOICES],
			i % MH_MIXER_VOICES, &sample, 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);

	count = 0;

	for (i = 0; i < 2 * MH_MIXER_VOICES; i++)
		count += (unsigned int)banks[i / MH_MIXER_VOICES]
				->voices[i % MH_MIXER_VOICES]
				.active;

	CHECK(count == 32 && !banks[1]->voices[0].active);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	count = 0;

	for (i = 0; i < 2 * MH_MIXER_VOICES; i++)
		count += (unsigned int)banks[i / MH_MIXER_VOICES]
				->voices[i % MH_MIXER_VOICES]
				.active;

	CHECK(count == 8);

	for (i = 0; i < MH_MIXER_VOICES; i++)
		mh_mixer_stop(banks[0], i);

	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == 0);
}

static void
consumers(void)
{
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects;
	struct mh_race_sound race, saved_race;
	struct mh_surface_sound surface, saved_surface;
	struct mh_mixer ambient, saved_ambient, *banks[TEST_BANKS];
	struct mh_sound_bank race_bank;
	static struct mh_surface_bank surface_bank;
	struct mh_ambient_bank ambient_bank;
	struct mh_sound_listener listener;
	static struct mh_field field;
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	unsigned int i;

	constant_wave(&sample, pcm);
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	memset(&race_bank, 0, sizeof(race_bank));
	memset(&surface_bank, 0, sizeof(surface_bank));
	memset(&ambient_bank, 0, sizeof(ambient_bank));
	CHECK(mh_sound_pool_init(TEST_BANKS * MH_MIXER_VOICES, &pool, NULL) ==
			MH_ERROR_NONE &&
		mh_effects_init(22050, .125, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_sound_init(22050, .125, &race, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_init(22050, .125, &surface, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(22050, .125, &ambient, NULL) == MH_ERROR_NONE);
	banks[0] = &race.mixer;
	banks[1] = &surface.mixer;
	banks[2] = &ambient;

	for (i = 0; i < TEST_BANKS; i++)
		CHECK(mh_mixer_bind(banks[i], &pool, i * MH_MIXER_VOICES,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		race_bank.waves[i] = sample;

	field.count = 1;
	field.cars[0].vehicle.engine.rpm = 1000;
	field.cars[0].camera_mode = MH_CAMERA_IN_CAR;
	race_bank.waves[MH_SOUND_INSIDE].pcm = NULL;
	saved_pool = pool;
	saved_race = race;
	CHECK(mh_race_sound_update(&race, &race_bank, &field, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&race, &saved_race, sizeof(race)) == 0);
	race_bank.waves[MH_SOUND_INSIDE] = sample;
	CHECK(mh_race_sound_update(&race, &race_bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.mixer.pool == &pool &&
		pool.records[0].request.priority == 2 &&
		pool.records[6].request.priority == 2);
	field.cars[0].race.split_hold_ms = 4000;
	field.cars[0].race.split_notice = 2;
	CHECK(mh_race_sound_checkpoint(&race, &race_bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[19].request.priority == 3);
	field.cars[0].race.finished = 1;
	CHECK(mh_race_sound_update(&race, &race_bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[18].request.priority == 4 &&
		pool.records[28].request.priority == 4);
	surface_bank.count = 4;
	surface_bank.materials.count = 1;

	for (i = 0; i < 4; i++)
		surface_bank.waves[i] = sample;

	strcpy(surface_bank.materials.entries[0]
			.sounds[MH_MATERIAL_CONTACT]
			.sample,
		"road");
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].pitch = 1;
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].volume =
		1;
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].priority =
		7;
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].flags = 1;
	field.cars[0].vehicle.grip.wheels[0].supported = 1;
	field.cars[0].vehicle.motion.velocity[2] = 10;
	CHECK(mh_surface_sound_update(&surface, &surface_bank, &field, 1, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(surface.mixer.pool == &pool &&
		pool.records[MH_MIXER_VOICES].request.priority == 7);
	saved_surface = surface;
	saved_pool = pool;
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].volume =
		101;
	CHECK(mh_surface_sound_update(&surface, &surface_bank, &field, 1, 0,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&surface, &saved_surface, sizeof(surface)) == 0);

	/*
	 * Crash is a nested transaction inside the surface update: a bad sample
	 * must also undo preceding contact requests and retain the shared RNG.
	 */
	surface_bank.materials.entries[0].sounds[MH_MATERIAL_CONTACT].volume =
		1;
	surface_bank.crashes.samples[6].enabled = 1;
	surface_bank.crashes.samples[6].wave = sample;
	surface_bank.crashes.samples[6].wave.pcm = NULL;
	surface_bank.crashes.samples[6].pitch = 1;
	surface_bank.crashes.samples[6].priority = 5;
	field.cars[0].ticks++;
	field.cars[0].impact_effects.contact = 1;
	field.cars[0].impact_effects.sound_class = 2;
	field.cars[0].impact_effects.severity = 60;
	CHECK(mh_surface_sound_update(&surface, &surface_bank, &field, 1, 0,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&surface, &saved_surface, sizeof(surface)) == 0);
	surface_bank.crashes.samples[6].wave = sample;
	CHECK(mh_surface_sound_update(&surface, &surface_bank, &field, 1, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(surface.mixer.pool == &pool &&
		pool.records[MH_MIXER_VOICES + 16].request.priority == 5 &&
		pool.records[MH_MIXER_VOICES + 16].active);
	memset(&listener, 0, sizeof(listener));
	listener.right[0] = listener.forward[2] = 1;
	ambient_bank.count = 2;

	for (i = 0; i < 2; i++) {
		ambient_bank.sounds[i].kind = MH_ENV_FIXED;
		ambient_bank.sounds[i].position[2] = 1;
		ambient_bank.sounds[i].near_distance = 10;
		ambient_bank.sounds[i].far_distance = 100;
		ambient_bank.sounds[i].volume = ambient_bank.sounds[i].pitch =
			1;
		ambient_bank.sounds[i].priority = 6;
		ambient_bank.waves[i] = sample;
	}

	ambient_bank.waves[1].pcm = NULL;
	saved_ambient = ambient;
	saved_pool = pool;
	CHECK(mh_ambient_update(&ambient, &ambient_bank, &listener, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&ambient, &saved_ambient, sizeof(ambient)) == 0);
	ambient_bank.waves[1] = sample;
	CHECK(mh_ambient_update(&ambient, &ambient_bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(ambient.pool == &pool &&
		pool.records[2 * MH_MIXER_VOICES].request.priority == 6);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
}

static void
crashes(void)
{
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, saved_effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS], saved_mixer;
	struct mh_crash_bank bank;
	struct mh_crash_sound sound, saved_sound;
	struct mh_impact_effects impact;
	struct mh_audio_pcm first, second;
	unsigned char first_pcm[SAMPLE_BYTES], second_pcm[SAMPLE_BYTES];
	unsigned long random, saved_random;
	unsigned int i;
	float output[1024];

	constant_wave(&first, first_pcm);
	constant_wave(&second, second_pcm);

	for (i = 0; i < SAMPLE_FRAMES; i++) {
		second_pcm[i * 2] = 0;
		second_pcm[i * 2 + 1] = 224;
	}

	memset(&bank, 0, sizeof(bank));
	memset(&impact, 0, sizeof(impact));

	for (i = 0; i < MH_CRASH_SAMPLES; i++) {
		bank.samples[i].enabled = 1;
		bank.samples[i].wave = i < 4 ? first : second;
		bank.samples[i].pitch = 1;
		bank.samples[i].priority = 2;
	}

	bind_banks(&pool, &effects, mixers, banks);
	mh_crash_sound_init(&sound);
	random = 13;
	impact.contact = 1;
	impact.severity = 15;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 2,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == .046875f);
	impact.sound_class = 1;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(pool.channels[0] == 1 && pool.records[0].channel == 0 &&
		pool.records[0].score == 0);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.channels[0] == 1 && pool.channels[1] == 1 &&
		pool.records[0].channel == 2);
	CHECK(effects.pcm.voices[0].wave == &bank.samples[0].wave &&
		effects.pcm.voices[0].cursor == 2);
	CHECK(effects.pcm.voices[1].wave == &bank.samples[5].wave &&
		effects.pcm.voices[1].cursor == 0);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 2,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == .0234375f);
	CHECK(banks[0]->voices[0].cursor == 2 &&
		effects.pcm.voices[0].cursor == 4);

	/*
	 * A bad replacement cannot release the old physical sample or alter
	 * the record aliases, selection counter or logical definition.
	 */
	impact.sound_class = 2;
	bank.samples[6].wave.pcm = NULL;
	saved_pool = pool;
	saved_effects = effects;
	saved_mixer = *banks[0];
	saved_sound = sound;
	saved_random = random;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
	CHECK(memcmp(banks[0], &saved_mixer, sizeof(saved_mixer)) == 0 &&
		memcmp(&sound, &saved_sound, sizeof(sound)) == 0);
	CHECK(random == saved_random);
	bank.samples[6].wave = second;

	/*
	 * Only the current channel stops. Old PCM continues with its own
	 * sample, cursor and gains; its stale table slot survives physical
	 * completion.
	 */
	CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.channels[0] == 1 && pool.channels[1] == 0 &&
		!pool.records[0].active);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 252,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == .046875f);
	CHECK(!effects.pcm.voices[0].active && pool.channels[0] == 1);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		pool.channels[0] == 1);

	/*
	 * Two copies before the callback: the existing first queue pointer is
	 * retained without setting active again. It still starts the new PCM.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	mh_crash_sound_init(&sound);
	random = 13;
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	impact.sound_class = 1;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(pool.pending[0] == 1 && !pool.records[0].active);
	CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		pool.records[0].channel == 1);
	CHECK(!banks[0]->voices[0].active && effects.pcm.voices[0].active);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == -.0234375f);
	CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		effects.pcm.voices[0].active);

	/*
	 * A full queue declines the copy and leaves the old sample playing
	 * until another record wins its channel. The copied zero score permits
	 * that.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	mh_crash_sound_init(&sound);
	random = 13;
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 32; i++) {
		CHECK(mh_mixer_policy(banks[1], i, 0, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[1], i, &first, 1, 1, 1, 0, 0, NULL) ==
			MH_ERROR_NONE);
	}

	impact.sound_class = 1;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(!pool.records[0].active && pool.channels[0] == 1 &&
		effects.pcm.voices[0].active);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.channels[0] == MH_MIXER_VOICES + 8 &&
		effects.pcm.voices[0].wave == &bank.samples[0].wave);

	/*
	 * The original backend refuses a start on a still-playing channel even
	 * though the scheduler publishes its new owner. Refresh applies the new
	 * owner's frequency/gains/loop flag to the old sample without
	 * restarting.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	mh_crash_sound_init(&sound);
	random = 13;
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);

	for (i = 1; i < 8; i++) {
		CHECK(mh_mixer_policy(banks[0], i, 1, 3, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], i, &first, 1, 0, 0, 0, 256,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 2,
		0, NULL) == MH_ERROR_NONE);
	impact.sound_class = 1;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		!pool.records[0].active);
	second.rate = 44100;
	CHECK(mh_mixer_policy(banks[0], 8, 1, 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 8, &second, 1, .25, .5, 2, 6, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		pool.channels[0] == 9);
	CHECK(effects.records[0] == 9 &&
		effects.pcm.voices[0].wave == &bank.samples[0].wave);
	CHECK(effects.pcm.voices[0].cursor == 2 &&
		effects.pcm.voices[0].pitch == 2);
	CHECK(effects.pcm.voices[0].loop_start == 0 &&
		effects.pcm.voices[0].loop_end == 256);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE);
	CHECK(output[0] == .015625f && output[1] == .03125f &&
		effects.pcm.voices[0].cursor == 4);
	mh_mixer_stop(banks[0], 8);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		!effects.pcm.voices[0].active);

	/*
	 * If the current channel is below an older alias, stealing it leaves
	 * that old alias adjusting the new logical owner later in the same
	 * walk.
	 */
	bind_banks(&pool, &effects, mixers, banks);
	mh_crash_sound_init(&sound);
	random = 13;
	CHECK(mh_mixer_policy(banks[0], 1, 1, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 1, &first, 1, 0, 0, 0, 256, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
			MH_ERROR_NONE &&
		pool.records[0].channel == 2);
	mh_mixer_stop(banks[0], 1);
	impact.sound_class = 1;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(!pool.records[0].active && pool.records[0].channel == 1 &&
		pool.channels[1] == 1);

	for (i = 2; i < 8; i++) {
		CHECK(mh_mixer_policy(banks[0], i, 1, 3, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], i, &first, 1, 0, 0, 0, 256,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_policy(banks[0], 8, 0, 4, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 8, &second, 1, .25, .5, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.channels[0] == 9 && pool.channels[1] == 1 &&
		pool.records[0].channel == 1);
	CHECK(effects.records[0] == 9 &&
		effects.pcm.voices[0].wave == &bank.samples[1].wave);
	CHECK(effects.pcm.voices[0].pitch == 1 &&
		effects.pcm.voices[0].left == .75);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE &&
		output[0] == .09375f);

	/*
	 * Repeated collisions and mixed-priority requests must remain
	 * renderable through aliases, busy-channel starts, stops and physical
	 * completion.
	 */
	for (i = 0; i < 2048; i++) {
		impact.sound_class = (i * 7) % 16;
		impact.severity = i % 3 == 0 ? 60 : 15;
		CHECK(mh_crash_update(&sound, banks[0], 0, &bank, &impact,
			&random, NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_policy(banks[1], i % 32, 0, i % 5, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[1], i % 32, i % 2 ? &first : &second,
			1, .5, .5, 0, 0, NULL) == MH_ERROR_NONE);

		if (i % 7 == 0)
			mh_mixer_stop(banks[0], 0);

		if (i % 3 == 0)
			CHECK(mh_effects_update(&effects, &pool, banks,
				TEST_BANKS, NULL) == MH_ERROR_NONE);

		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			output, 64, 0, NULL) == MH_ERROR_NONE);
	}
}

static void
clear_aliases(void)
{
	static const struct alias_expectation expected[ALIAS_SCENARIOS] = {
		{0, 0, 1}, {0, 1, 0}, {0, 0, 3}, {1, 0, 0}, {1, 0, 1}};

	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, saved_effects;
	struct mh_mixer mixers[TEST_BANKS], saved_mixers[TEST_BANKS],
		*banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	unsigned int scenario, i, count, mask;

	constant_wave(&sample, pcm);

	for (scenario = 0; scenario < ALIAS_SCENARIOS; scenario++) {
		bind_banks(&pool, &effects, mixers, banks);
		CHECK(mh_mixer_play(banks[0], 0, &sample, 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);
		pool.pending[0] = pool.pending[1] = 0;
		pool.records[0].active = pool.records[MH_MIXER_VOICES].active =
			0;

		if (scenario == INACTIVE_PENDING)
			pool.pending[0] = 1;
		else if (scenario == UNREACHABLE_ACTIVE) {
			pool.records[0].active = 1;
			pool.records[0].channel = 1;
		} else {
			pool.channels[0] = pool.channels[1] = 1;
			pool.records[0].channel =
				scenario == INACTIVE_ALIASES ? 1 : 2;
			pool.records[0].active = scenario != INACTIVE_ALIASES;

			if (scenario == CROSS_LINKED_CHANNELS) {
				pool.channels[1] = MH_MIXER_VOICES + 1;
				pool.records[MH_MIXER_VOICES].active = 1;
				pool.records[MH_MIXER_VOICES].channel = 1;
			}

			for (i = 0; i < 2; i++) {
				CHECK(mh_mixer_play(&effects.pcm, i, &sample, 1,
					1, 1, 0, 0,
					NULL) == MH_ERROR_NONE);
				effects.records[i] = pool.channels[i];
				effects.loop_end[i] = sample.frames;
			}
		}

		CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS,
			NULL) == MH_ERROR_NONE);
		count = 0;

		for (i = 0; i < pool.count; i++)
			count += pool.records[i].active;

		CHECK(count == expected[scenario].active_records);
		count = 0;

		for (i = 0; i < MH_SOUND_PENDING; i++)
			count += pool.pending[i] != 0;

		CHECK(count == expected[scenario].pending_records);
		mask = 0;

		for (i = 0; i < MH_SOUND_CHANNELS; i++)
			if (effects.pcm.voices[i].active)
				mask |= 1U << i;

		CHECK(mask == expected[scenario].physical_mask);
		CHECK(banks[0]->voices[0].active ==
			(int)pool.records[0].active);
		CHECK(banks[1]->voices[0].active ==
			(int)pool.records[MH_MIXER_VOICES].active);
		CHECK(banks[0]->voices[0].wave == &sample &&
			banks[1]->voices[0].wave == &sample);

		if (scenario == CROSS_LINKED_CHANNELS)
			CHECK(pool.channels[0] == 1 && pool.channels[1] == 0 &&
				pool.records[MH_MIXER_VOICES].active &&
				pool.records[MH_MIXER_VOICES].channel == 1);

		saved_pool = pool;
		saved_effects = effects;
		memcpy(saved_mixers, mixers, sizeof(mixers));
		CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
			memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
		CHECK(memcmp(mixers, saved_mixers, sizeof(mixers)) == 0);
		pool.pending[31] = pool.count + 1;
		saved_pool = pool;
		CHECK(mh_effects_clear(&effects, &pool, banks, TEST_BANKS,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
			memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
		CHECK(memcmp(mixers, saved_mixers, sizeof(mixers)) == 0);
	}
}

static void
rates(void)
{
	struct mh_effect_rate rate, cap, saved;
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, saved_effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	float output[2];
	double step;

	CHECK(mh_effects_rate(-1, 256, &rate, NULL) == MH_ERROR_NONE &&
		rate.whole == 1 && rate.fraction == 0);
	CHECK(mh_effects_rate(0, 256, &rate, NULL) == MH_ERROR_NONE &&
		rate.whole == 1 && rate.fraction == 0);
	CHECK(mh_effects_rate(44100, 1, &rate, NULL) == MH_ERROR_NONE &&
		rate.whole == 1 && rate.fraction == 0);
	CHECK(mh_effects_rate(33075, 1, &rate, NULL) == MH_ERROR_NONE &&
		rate.whole == 0 && rate.fraction == MH_U32_SIGN_BIT);
	CHECK(mh_effects_rate(200000, 256, &cap, NULL) == MH_ERROR_NONE &&
		cap.whole == 9);
	CHECK(mh_effects_rate(3072000, 256, &rate, NULL) == MH_ERROR_NONE &&
		rate.whole == cap.whole && rate.fraction == cap.fraction);
	saved = rate;
	CHECK(mh_effects_rate(1, 0, &rate, NULL) != MH_ERROR_NONE &&
		memcmp(&rate, &saved, sizeof(rate)) == 0);
	CHECK(mh_effects_rate(1, 32000001UL, &rate, NULL) != MH_ERROR_NONE &&
		memcmp(&rate, &saved, sizeof(rate)) == 0);
	CHECK(mh_effects_rate(1, 1, NULL, NULL) != MH_ERROR_NONE);

	/*
	 * The real bank adapter applies the cap to sample-rate times pitch,
	 * preserves the logical pitch, and uses the retained physical span.
	 */
	constant_wave(&sample, pcm);
	sample.rate = 192000;
	bind_banks(&pool, &effects, mixers, banks);
	CHECK(mh_mixer_play(banks[0], 0, &sample, 16, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.steps[0].whole == cap.whole &&
		effects.steps[0].fraction == cap.fraction);
	CHECK(banks[0]->voices[0].pitch == 16);
	step = cap.whole + cap.fraction / (double)MH_U32_MODULUS;
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].cursor == step &&
		banks[0]->voices[0].cursor == step);
	CHECK(mh_mixer_adjust(banks[0], 0, -1, 1, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.steps[0].whole == 1 && effects.steps[0].fraction == 0 &&
		banks[0]->voices[0].pitch == -1);
	CHECK(effects.pcm.voices[0].cursor == step);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 1,
		0, NULL) == MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].cursor == step + 1);
	CHECK(mh_mixer_play(banks[0], 0, &sample, 16, 1, 1, 2, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.steps[0].whole == 1 &&
		effects.steps[0].fraction == cap.fraction);
	banks[0]->voices[0].loop_start = banks[0]->voices[0].loop_end = 0;
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.steps[0].whole == 1 &&
		effects.steps[0].fraction == cap.fraction);
	CHECK(effects.loop_start[0] == 2 && effects.loop_end[0] == 4 &&
		effects.pcm.voices[0].loop_end == 0);
	saved_pool = pool;
	saved_effects = effects;
	banks[0]->voices[0].pitch = (double)FLT_MAX * 2;
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
}

static void
output_rates(void)
{
	static const unsigned long rates[] = {
		4000, 8000, 11025, 22050, 44100, 48000, 192000};

	static float whole[8192 * 2], pieces[8192 * 2];
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, initial, finished;
	struct mh_mixer mixers[TEST_BANKS], saved_mixers[TEST_BANKS],
		*banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	unsigned int r, i, value;
	size_t done, chunk;

	constant_wave(&sample, pcm);
	sample.rate = 33075;

	for (i = 0; i < SAMPLE_FRAMES; i++) {
		value = (i * 259UL + 19) & (unsigned long)MH_U16_MAX;
		pcm[i * 2] = (unsigned char)value;
		pcm[i * 2 + 1] = (unsigned char)(value >> 8);
	}

	for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
		bind_banks(&pool, &effects, mixers, banks);
		CHECK(mh_effects_init(rates[r], .125, &effects, NULL) ==
			MH_ERROR_NONE);

		for (i = 0; i < TEST_BANKS; i++)
			mixers[i].rate = rates[r];

		CHECK(mh_mixer_policy(banks[0], 0, 1, 4, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], 0, &sample, 1, 1, .5, 0,
			sample.frames, NULL) == MH_ERROR_NONE);
		CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS,
			NULL) == MH_ERROR_NONE);
		initial = effects;
		saved_pool = pool;
		memcpy(saved_mixers, mixers, sizeof(mixers));
		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			whole, 8192, 0, NULL) == MH_ERROR_NONE);
		finished = effects;
		effects = initial;
		pool = saved_pool;
		memcpy(mixers, saved_mixers, sizeof(mixers));
		done = 0;

		while (done < 8192) {
			chunk = done % 251 + 1;

			if (chunk > 8192 - done)
				chunk = 8192 - done;

			CHECK(mh_effects_render(&effects, &pool, banks,
				TEST_BANKS, pieces + done * 2, chunk, 0,
				NULL) == MH_ERROR_NONE);
			done += chunk;
		}

		CHECK(memcmp(whole, pieces, sizeof(whole)) == 0);
		CHECK(effects.cursors[0] == finished.cursors[0] &&
			effects.fractions[0] == finished.fractions[0]);
		CHECK(effects.phase == finished.phase &&
			effects.held[0] == finished.held[0] &&
			effects.held[1] == finished.held[1]);
		initial = effects;
		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			pieces, 4, 1, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&effects, &initial, sizeof(effects)) == 0);

		for (i = 0; i < 8; i++)
			CHECK(pieces[i] == 0);

		/*
		 * Invalid PCM must leave the output, exact cursors and
		 * resampling neighbors untouched even after successful
		 * streaming.
		 */
		sample.bytes = 511;
		pieces[0] = .75f;
		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			pieces, 4, 0, NULL) != MH_ERROR_NONE);
		CHECK(pieces[0] == .75f &&
			memcmp(&effects, &initial, sizeof(effects)) == 0);
		sample.bytes = 512;
		effects.pcm.voices[0].left = 2;
		initial = effects;
		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			pieces, 4, 0, NULL) != MH_ERROR_NONE);
		CHECK(pieces[0] == .75f &&
			memcmp(&effects, &initial, sizeof(effects)) == 0);
		effects.pcm.voices[0].left = 1;
		mh_effects_stop_device(&effects);
		CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS,
			pieces, 4, 0, NULL) == MH_ERROR_NONE);

		for (i = 0; i < 8; i++)
			CHECK(pieces[i] == 0);
	}
}

static void
producer_stops(void)
{
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects, saved_effects;
	struct mh_mixer mixers[TEST_BANKS], *banks[TEST_BANKS];
	struct mh_audio_pcm sample;
	unsigned char pcm[SAMPLE_BYTES];
	float output[16];

	constant_wave(&sample, pcm);
	bind_banks(&pool, &effects, mixers, banks);
	CHECK(mh_mixer_play(banks[0], 0, &sample, 1, 1, 1, 0, 256, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[0], 1, &sample, 1, 1, 1, 0, 256, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, TEST_BANKS, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, TEST_BANKS, output, 8,
		0, NULL) == MH_ERROR_NONE);
	mh_mixer_stop(banks[0], 0);
	CHECK(mh_mixer_adjust(banks[0], 1, 2, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(banks[1], 0, &sample, 1, 1, 1, 0, 0, NULL) ==
		MH_ERROR_NONE);
	saved_pool = pool;
	CHECK(mh_effects_flush_stops(&effects, &pool, NULL) == MH_ERROR_NONE);
	CHECK(!effects.pcm.voices[0].active && effects.records[0] == 0);
	CHECK(effects.pcm.voices[1].active && effects.pcm.voices[1].left == 1 &&
		effects.steps[1].whole == 1 && effects.cursors[1] == 8);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		pool.pending[0] == MH_MIXER_VOICES + 1);

	/*
	 * Replacing a shared definition is not an explicit stop. Its old
	 * physical voice survives even though the record loses its channel.
	 */
	CHECK(mh_mixer_copy(banks[0], 1, &sample, 1, .5, .5, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_flush_stops(&effects, &pool, NULL) == MH_ERROR_NONE);
	CHECK(effects.records[1] == 2 && effects.pcm.voices[1].active &&
		effects.cursors[1] == 8);
	saved_effects = effects;
	pool.channels[7] = pool.count + 1;
	CHECK(mh_effects_flush_stops(&effects, &pool, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&effects, &saved_effects, sizeof(effects)) == 0);
}

int
main(void)
{

	channel_admission();
	loop_refresh();
	completion_admission();
	pending_limit();
	consumers();
	crashes();
	clear_aliases();
	rates();
	output_rates();
	producer_stops();

	return failures != 0;
}
