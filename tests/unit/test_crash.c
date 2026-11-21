#include <stdio.h>
#include <string.h>

#include "engine/audio/surface.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_surface_bank bank;
static struct mh_field field;
static unsigned char pcm[64];

static int
selection(void)
{
	struct mh_crash_sound sound, saved;
	struct mh_impact_effects impact, before;
	unsigned long random;
	int play;
	double relative[3] = {-15, 7, 9}, normal[3] = {1, 0, 0}, severity;

	memset(&impact, 0, sizeof(impact));
	mh_crash_sound_init(&sound);
	random = 13;
	CHECK(mh_impact_severity(relative, normal, &severity, NULL) ==
			MH_ERROR_NONE &&
		severity == 30);
	CHECK(mh_impact_add(&impact, 0, severity, NULL) == MH_ERROR_NONE &&
		impact.contact && impact.severity == 30);
	CHECK(mh_impact_add(&impact, 3, -100, NULL) == MH_ERROR_NONE &&
		impact.sound_class == 3 && impact.severity == 30);
	before = impact;
	CHECK(mh_impact_add(&impact, 16, 50, NULL) != MH_ERROR_NONE &&
		memcmp(&impact, &before, sizeof(impact)) == 0);

	/* A missing class before any sample has been copied is silent. */
	CHECK(mh_crash_select(&sound, &impact, 0, 0, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		!play && sound.bound == -1);
	impact.sound_class = 0;
	impact.severity = 0;
	CHECK(mh_crash_select(&sound, &impact, 0, 0, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		play && sound.bound == 0 && sound.gain == .5);
	CHECK(random == 326);
	impact.severity = 60;
	CHECK(mh_crash_select(&sound, &impact, 0, 1, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		!play && sound.gain == .5 && random == 326);
	CHECK(mh_crash_select(&sound, &impact, 0, 0, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		play && sound.bound == 3 && sound.gain == 1);
	impact.sound_class = 2;
	CHECK(mh_crash_select(&sound, &impact, 4, 1, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		play && sound.bound == 6 && sound.gain == 1.5);
	CHECK(random == 639);
	impact.sound_class = 3;
	impact.severity = 0;
	CHECK(mh_crash_select(&sound, &impact, 4, 1, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		!play && sound.bound == 6 && sound.gain == 1.5);
	CHECK(sound.last_class == 3);
	CHECK(mh_crash_select(&sound, &impact, 4, 0, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		play && sound.gain == 1.5);
	impact.contact = 0;
	CHECK(mh_crash_select(&sound, &impact, 4, 0, &random, &play, NULL) ==
			MH_ERROR_NONE &&
		!play);
	saved = sound;
	impact.severity = -1;
	play = 71;
	CHECK(mh_crash_select(&sound, &impact, 4, 0, &random, &play, NULL) !=
			MH_ERROR_NONE &&
		play == 71 && random == 639);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0);

	return 0;
}

static int
playback(void)
{
	struct mh_surface_sound sound, before;
	struct mh_crash_sample *sample;
	float output[256];
	unsigned int i;
	double cursor;

	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	bank.count = 7;
	bank.materials.count = 1;
	field.count = 2;

	for (i = 0; i < MH_CRASH_SAMPLES; i++) {
		sample = &bank.crashes.samples[i];
		sample->enabled = i < 4 || i == 6;
		sample->pitch = i < 2 ? (double)(float).8 : i == 6 ? 0 : 1;
		sample->volume = .01; /* The runtime overwrites ESD volume. */
		sample->wave.pcm = pcm;
		sample->wave.bytes = 64;
		sample->wave.frames = 32;
		sample->wave.channels = 1;
		sample->wave.bits = 16;
		sample->wave.rate = 11025;
	}

	CHECK(mh_surface_sound_init(22050, 1, &sound, NULL) == MH_ERROR_NONE);
	field.cars[1].impact_effects.contact = 1;
	field.cars[1].impact_effects.severity = 100;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active);
	field.cars[0].ticks++;
	CHECK(mh_impact_add(&field.cars[0].impact_effects, 0, 15, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active);
	CHECK(sound.mixer.voices[MH_SURFACE_VOICE_CRASH].wave ==
			&bank.crashes.samples[0].wave &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].left == .75);
	CHECK(mh_mixer_render(&sound.mixer, output, 10, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor;
	field.cars[0].ticks++;
	field.cars[0].impact_effects.severity = 100;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == cursor);
	CHECK(sound.random == 326 &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].left == .75);
	before = sound;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 10, 1, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == cursor);
	CHECK(mh_mixer_render(&sound.mixer, output, 128, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active);

	/*
	 * Completion permits a retained contact to play again without a new
	 * physics tick. An active same-class record still retains its cursor.
	 */
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active &&
		sound.crash.bound == 3 && sound.random == 639);
	CHECK(mh_mixer_render(&sound.mixer, output, 10, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == cursor &&
		sound.random == 639);
	field.cars[0].ticks++;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active &&
		sound.crash.bound == 3);
	field.cars[0].ticks++;
	field.cars[0].impact_effects.sound_class = 2;
	field.cars[0].impact_effects.severity = 60;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.crash.bound == 6 && sound.crash.gain == 1.5);
	CHECK(sound.mixer.voices[MH_SURFACE_VOICE_CRASH].left == 1 &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].pitch == 2 &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 10, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == 10);
	field.cars[0].ticks++;
	field.cars[0].impact_effects.sound_class = 3;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].cursor == 10);
	CHECK(mh_mixer_render(&sound.mixer, output, 32, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active);
	field.cars[0].ticks++;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].active);
	CHECK(sound.crash.bound == 6 &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].pitch == 2 &&
		sound.crash.gain == 1.5);

	/* Invalid replacement is transactional, including the shared RNG. */
	field.cars[0].ticks++;
	field.cars[0].impact_effects.sound_class = 0;
	before = sound;
	bank.crashes.samples[2].wave.pcm = NULL;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);

	return 0;
}

static int
shared_random(void)
{
	struct mh_surface_sound sound;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	field.count = 1;

	for (i = 0; i < 4; i++)
		bank.waves[i] = bank.crashes.samples[3].wave;

	bank.materials.entries[0].slide_type = 1;
	field.cars[0].vehicle.grip.wheels[0].supported = 1;
	field.cars[0].impact_effects.contact = 1;
	CHECK(mh_surface_sound_init(22050, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&sound.mixer, 3, &bank.waves[0], 1, 1, 1, 0, 32,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Slide release consumes the first choice; the crash consumes the next.
	 */
	CHECK(sound.random == 639 && sound.mixer.voices[4].active &&
		!sound.mixer.voices[5].active);
	CHECK(sound.crash.bound == 1 &&
		sound.mixer.voices[MH_SURFACE_VOICE_CRASH].wave ==
			&bank.crashes.samples[1].wave);

	return 0;
}

static int
unloaded(void)
{
	struct mh_crash_sound sound, saved;
	struct mh_impact_effects impact;
	struct mh_mixer mixer;
	struct mh_sound_pool pool, before;
	struct mh_sound_changes changes;
	unsigned long random;
	unsigned int i, stopped;

	mh_crash_sound_init(&sound);
	memset(&impact, 0, sizeof(impact));
	CHECK(mh_mixer_init(48000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&mixer, &pool, 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_CRASH_SAMPLES; i++)
		bank.crashes.samples[i].wave.pcm = pcm;

	random = 13;
	impact.contact = 1;
	impact.severity = 15;
	CHECK(mh_crash_update(&sound, &mixer, MH_SURFACE_VOICE_CRASH,
		&bank.crashes, &impact, &random, NULL) == MH_ERROR_NONE);
	CHECK(sound.bound == 0 && random == 326 &&
		mixer.voices[MH_SURFACE_VOICE_CRASH].active);
	CHECK(mh_crash_sound_unload(&sound,
		mixer.voices[MH_SURFACE_VOICE_CRASH].active,
		NULL) == MH_ERROR_NONE);
	CHECK(sound.detached && sound.bound == -1 && sound.last_class == 0 &&
		sound.gain == .75);
	mh_mixer_stop(&mixer, MH_SURFACE_VOICE_CRASH);
	random = 13;
	CHECK(mh_sound_pool_clear(&pool, &stopped, NULL) == MH_ERROR_NONE &&
		stopped == 0);
	CHECK(mh_crash_update(&sound, &mixer, MH_SURFACE_VOICE_CRASH,
		&bank.crashes, &impact, &random, NULL) == MH_ERROR_NONE);
	CHECK(sound.detached && !mixer.voices[MH_SURFACE_VOICE_CRASH].active &&
		random == 13);

	/*
	 * A missing class changes the remembered class without loading a
	 * sample. Repeated unloads and channel stops cannot clear detached
	 * activity.
	 */
	impact.sound_class = 3;
	CHECK(mh_crash_update(&sound, &mixer, MH_SURFACE_VOICE_CRASH,
		&bank.crashes, &impact, &random, NULL) == MH_ERROR_NONE);
	CHECK(sound.last_class == 3 && sound.detached && sound.bound == -1);
	CHECK(mh_crash_sound_unload(&sound, 0, NULL) == MH_ERROR_NONE &&
		sound.detached);
	saved = sound;
	before = pool;
	CHECK(mh_crash_sound_unload(&sound, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&sound, &saved, sizeof(sound)) == 0);
	CHECK(mh_crash_sound_unload(NULL, 0, NULL) != MH_ERROR_NONE);
	impact.sound_class = 0;
	bank.crashes.samples[0].wave.pcm = NULL;
	CHECK(mh_crash_update(&sound, &mixer, MH_SURFACE_VOICE_CRASH,
		&bank.crashes, &impact, &random, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&sound, &saved, sizeof(sound)) == 0 &&
		memcmp(&pool, &before, sizeof(pool)) == 0 && random == 13);
	bank.crashes.samples[0].wave.pcm = pcm;
	CHECK(mh_crash_update(&sound, &mixer, MH_SURFACE_VOICE_CRASH,
		&bank.crashes, &impact, &random, NULL) == MH_ERROR_NONE);
	CHECK(!sound.detached && sound.bound == 0 &&
		mixer.voices[MH_SURFACE_VOICE_CRASH].active && random == 326);
	CHECK(mh_sound_pool_update(&pool, 0, &changes, NULL) == MH_ERROR_NONE &&
		pool.records[MH_SURFACE_VOICE_CRASH].channel != 0);

	return 0;
}

static int
absent_sample(void)
{
	struct mh_crash_sound sound;
	struct mh_impact_effects impact;
	struct mh_mixer mixer;
	struct mh_crash_bank samples;
	unsigned long random;

	samples = bank.crashes;
	memset(&impact, 0, sizeof(impact));
	mh_crash_sound_init(&sound);
	CHECK(mh_mixer_init(22050, 1, &mixer, NULL) == MH_ERROR_NONE);
	random = 13;
	impact.contact = 1;
	impact.severity = 15;
	CHECK(mh_crash_update(&sound, &mixer, 0, &samples, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mixer.voices[0].active);
	memset(&samples.samples[MH_CRASH_CAR_SAMPLES + 3], 0,
		sizeof(samples.samples[0]));
	samples.samples[MH_CRASH_CAR_SAMPLES + 3].enabled = 1;
	samples.samples[MH_CRASH_CAR_SAMPLES + 3].absent = 1;
	impact.sound_class = 3;
	CHECK(mh_crash_update(&sound, &mixer, 0, &samples, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(!mixer.voices[0].active && sound.last_class == 3 &&
		sound.bound == MH_CRASH_CAR_SAMPLES + 3);
	impact.sound_class = 0;
	CHECK(mh_crash_update(&sound, &mixer, 0, &samples, &impact, &random,
		NULL) == MH_ERROR_NONE);
	CHECK(mixer.voices[0].active);

	return 0;
}

int
main(void)
{
	return selection() || playback() || shared_random() || unloaded() ||
		absent_sample();
}
