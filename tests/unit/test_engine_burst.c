#include <stdio.h>
#include <string.h>

#include "engine/audio/effects.h"
#include "engine/audio/surface.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"

static struct mh_surface_bank bank;
static struct mh_field field;
static unsigned char pcm[512];
static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
setup_fixture(void)
{
	unsigned int i;

	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	bank.count = MH_SURFACE_FIXED_SAMPLES;
	bank.materials.count = 1;
	field.count = 8;
	field.active_mask = 255;

	for (i = 0; i < MH_SURFACE_FIXED_SAMPLES; i++) {
		bank.waves[i].pcm = pcm;
		bank.waves[i].bytes = sizeof(pcm);
		bank.waves[i].frames = sizeof(pcm) / 2;
		bank.waves[i].channels = 1;
		bank.waves[i].bits = 16;
		bank.waves[i].rate = 8000;
	}
}

static void
playback(void)
{
	struct mh_surface_sound sound, saved;
	struct mh_sound_pool pool, before;
	struct mh_effects_mixer effects;
	struct mh_mixer filler, *mixers[2];
	float output[512];
	unsigned int i;

	setup_fixture();
	field.count = 1;
	field.cars[0].vehicle.engine_braking = 1;
	CHECK(mh_surface_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.random == 326 &&
		sound.mixer.voices[18].wave == &bank.waves[7] &&
		!sound.mixer.voices[19].active);
	CHECK(sound.mixer.voices[18].pitch == 1 &&
		sound.mixer.voices[18].left == (double)(float).9);
	CHECK(sound.mixer.priority[18] == 2 && sound.mixer.flags[18] == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 5, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.random == 326 && sound.mixer.voices[18].cursor == 5);
	field.cars[0].vehicle.engine_braking = 0;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[18].active);
	CHECK(mh_mixer_render(&sound.mixer, output, 251, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[18].active);
	field.cars[0].vehicle.engine_braking = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.random == 639 &&
		sound.mixer.voices[19].wave == &bank.waves[8]);
	field.unavailable_mask = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[19].active);
	field.unavailable_mask = 0;
	saved = sound;
	bank.waves[7].pcm = NULL;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	bank.waves[7].pcm = pcm;
	field.cars[0].vehicle.engine_braking = 2;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);

	/*
	 * A full pending queue declines a burst but consumes its random choice.
	 * A later update can try again, using the next choice.
	 */
	setup_fixture();
	field.count = 1;
	field.cars[0].vehicle.engine_braking = 1;
	CHECK(mh_sound_pool_init(2 * MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_init(8000, 1, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_init(8000, 1, &filler, NULL) == MH_ERROR_NONE &&
		mh_surface_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&filler, &pool, 0, NULL) == MH_ERROR_NONE &&
		mh_mixer_bind(&sound.mixer, &pool, MH_MIXER_VOICES, NULL) ==
			MH_ERROR_NONE);
	mixers[0] = &filler;
	mixers[1] = &sound.mixer;

	for (i = 0; i < 32; i++)
		CHECK(mh_mixer_play(&filler, i, &bank.waves[0], 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.random == 326 && !sound.mixer.voices[18].active &&
		!sound.mixer.voices[19].active);
	CHECK(mh_effects_update(&effects, &pool, mixers, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.random == 639);
	CHECK(mh_effects_update(&effects, &pool, mixers, 2, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[MH_MIXER_VOICES + 19].active &&
		pool.records[MH_MIXER_VOICES + 19].channel != 0 &&
		pool.records[MH_MIXER_VOICES + 19].request.priority == 2);
	CHECK(mh_effects_render(&effects, &pool, mixers, 2, output, 256, 0,
		NULL) == MH_ERROR_NONE);

	/*
	 * The device samples at 22050 Hz before output conversion; drain the
	 * final source interval as well as the 256 requested 8000 Hz frames.
	 */
	CHECK(mh_effects_render(&effects, &pool, mixers, 2, output, 1, 0,
		NULL) == MH_ERROR_NONE);

	/* Completion is still pending refresh. */
	CHECK(sound.mixer.voices[19].active);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.random == 639);
	CHECK(mh_effects_update(&effects, &pool, mixers, 2, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[19].active);
	saved = sound;
	before = pool;
	bank.waves[7].pcm = NULL;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &before, sizeof(pool)) == 0);
}

static void
shared_random(void)
{
	struct mh_surface_sound sound;
	unsigned int i;

	setup_fixture();
	field.count = 1;

	for (i = 0; i < 4; i++) {
		bank.crashes.samples[i].enabled = 1;
		bank.crashes.samples[i].priority = 2;
		bank.crashes.samples[i].pitch = 1;
		bank.crashes.samples[i].wave = bank.waves[0];
	}

	field.cars[0].vehicle.engine_braking = 1;
	field.cars[0].vehicle.grip.wheels[0].supported = 1;
	field.cars[0].impact_effects.contact = 1;
	bank.materials.entries[0].slide_type = 1;
	CHECK(mh_surface_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&sound.mixer, 3, &bank.waves[0], 1, 1, 1, 0, 256,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	/* Original branch order: burst, skid release, then car crash. */
	CHECK(sound.random == 952 && sound.mixer.voices[18].active &&
		!sound.mixer.voices[19].active);
	CHECK(sound.mixer.voices[5].active && !sound.mixer.voices[4].active &&
		sound.crash.bound == 0);
}

int
main(void)
{

	playback();
	shared_random();

	return failures != 0;
}
