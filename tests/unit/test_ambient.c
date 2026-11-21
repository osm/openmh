#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/env_sound.h"
#include "engine/audio/ambient.h"
#include "engine/audio/effects.h"
#include "engine/core/error.h"
#include "engine/simulation/camera.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
parser(void)
{
	static const char good[] =
		"// emitters\r\nEnvSound Bird.wav 1 1.7 .9 0 100 -1 2 3 // comment\n"
		"objectenvsound 11 ship.wav 3 2.6 0 0 85\nObjectColSound 15 crash.wav 1 2 0";
	static const char *bad[] = {"Unknown a 1 2", "EnvSound",
		"EnvSound a 1 1 1 0 100 0 0",
		"EnvSound a 1 1 1 0 100 0 0 0 extra",
		"EnvSound ../a 1 1 1 0 100 0 0 0",
		"EnvSound a/b 1 1 1 0 100 0 0 0",
		"EnvSound a\\b 1 1 1 0 100 0 0 0",
		"EnvSound c:a 1 1 1 0 100 0 0 0",
		"EnvSound a -1 1 1 0 100 0 0 0",
		"EnvSound a 1.5 1 1 0 100 0 0 0",
		"EnvSound a 1 nan 1 0 100 0 0 0",
		"EnvSound a 1 1 0 0 100 0 0 0", "EnvSound a 1 1 17 0 100 0 0 0",
		"EnvSound a 1 1 1 100 100 0 0 0",
		"EnvSound a 1 1 1 -1 100 0 0 0",
		"EnvSound a 1 1 1 0 100 1e999 0 0",
		"EnvSound a 1 1 1 0 100 1e-999 0 0",
		"ObjectColSound 16 a 1 1 0",
		"ObjectEnvSound 65536 a 1 1 1 0 100",
		"ObjectColSound 1 a 1 1 0 2"};

	struct mh_env_sounds env, saved;
	char large[4096];
	unsigned int i;

	CHECK(mh_env_sounds_esd_decode((const unsigned char *)good,
		sizeof(good) - 1, &env, NULL) == MH_ERROR_NONE);
	CHECK(env.count == 3 && env.sounds[0].kind == MH_ENV_FIXED &&
		env.sounds[1].object_class == 11);
	CHECK(env.sounds[0].volume == (double)(float)1.7 &&
		env.sounds[0].position[0] == -1);
	CHECK(env.sounds[1].pitch == 0 && env.sounds[2].object_class == 15 &&
		env.sounds[2].kind == MH_ENV_COLLISION);
	saved = env;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(mh_env_sounds_esd_decode((const unsigned char *)bad[i],
			strlen(bad[i]), &env, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&env, &saved, sizeof(env)) == 0);
	}

	memset(large, 'x', sizeof(large));
	CHECK(mh_env_sounds_esd_decode((const unsigned char *)large,
		sizeof(large), &env, NULL) != MH_ERROR_NONE);
	memcpy(large, good, sizeof(good));
	large[4] = 0;
	CHECK(mh_env_sounds_esd_decode((const unsigned char *)large,
		sizeof(good) - 1, &env, NULL) != MH_ERROR_NONE);
	large[0] = 0;

	for (i = 0; i < 64; i++)
		strcat(large, "EnvSound a 1 1 1 0 100 0 0 0\n");

	CHECK(mh_env_sounds_esd_decode((const unsigned char *)large,
		strlen(large), &env, NULL) == MH_ERROR_NONE &&
		env.count == 64);
	saved = env;
	strcat(large, "EnvSound a 1 1 1 0 100 0 0 0\n");
	CHECK(mh_env_sounds_esd_decode((const unsigned char *)large,
		strlen(large), &env, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&env, &saved, sizeof(env)) == 0);
}

static void
spatial(void)
{
	struct mh_sound_listener listener;
	double eye[3] = {0, 0, 0}, target[3] = {0, 0, 1},

	source[3] = {0, 0, 10}, gains[2];

	mh_sound_listener_camera(eye, target, &listener);
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .6) < 1e-6 && gains[0] == gains[1]);
	source[2] = -10;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .36) < 1e-6 && gains[0] == gains[1]);
	source[0] = 10;
	source[2] = 0;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .16) < 1e-6 && fabs(gains[1] - .8) < 1e-6);
	source[0] = -10;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .8) < 1e-6 && fabs(gains[1] - .16) < 1e-6);
	source[0] = 0;
	source[1] = 55;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .24) < 1e-6 && gains[0] == gains[1]);
	source[1] = 100;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
			MH_ERROR_NONE &&
		gains[0] == 0);
	source[1] = 101;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
			MH_ERROR_NONE &&
		gains[0] == 0);
	source[1] = 0;
	CHECK(mh_sound_spatial(&listener, source, 10, 10, 100, gains, NULL) ==
			MH_ERROR_NONE &&
		gains[0] == 1);
	CHECK(mh_sound_spatial(&listener, source, 1, 100, 100, gains, NULL) !=
			MH_ERROR_NONE &&
		gains[0] == 1);
	target[0] = 1;
	target[2] = 0;
	mh_sound_listener_camera(eye, target, &listener);
	source[0] = 10;
	CHECK(mh_sound_spatial(&listener, source, 1, 10, 100, gains, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(gains[0] - .6) < 1e-6 && gains[0] == gains[1]);
	mh_sound_listener_camera(eye, eye, &listener);
	CHECK(listener.forward[2] == 1 && listener.right[0] == 1);
}

static void
playback(void)
{
	struct mh_ambient_bank bank;
	struct mh_mixer mixer, saved;
	struct mh_sound_listener listener;
	double eye[3] = {0, 0, 0}, target[3] = {0, 0, 1};
	unsigned char pcm[4] = {0, 64, 128, 255};
	float out[16];

	memset(&bank, 0, sizeof(bank));
	bank.count = 1;
	bank.sounds[0].pitch = bank.sounds[0].volume = 1;
	bank.sounds[0].far_distance = 100;
	bank.sounds[0].position[2] = 10;
	bank.waves[0].pcm = pcm;
	bank.waves[0].frames = bank.waves[0].bytes = 4;
	bank.waves[0].rate = 8000;
	bank.waves[0].channels = 1;
	bank.waves[0].bits = 8;
	mh_sound_listener_camera(eye, target, &listener);
	CHECK(mh_mixer_init(8000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
			MH_ERROR_NONE &&
		mixer.voices[0].active);
	CHECK(mh_mixer_render(&mixer, out, 3, 0, NULL) == MH_ERROR_NONE &&
		out[0] < -.5f && out[0] == out[1]);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
			MH_ERROR_NONE &&
		mixer.voices[0].cursor == 3);
	saved = mixer;
	bank.sounds[0].pitch = 1e100;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &mixer, sizeof(mixer)) == 0);
	bank.sounds[0].pitch = 1;
	bank.sounds[0].position[2] = 200;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
			MH_ERROR_NONE &&
		!mixer.voices[0].active);
	bank.sounds[0].position[2] = 10;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
			MH_ERROR_NONE &&
		mixer.voices[0].cursor == 0);
	bank.count = 0;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
			MH_ERROR_NONE &&
		!mixer.voices[0].active);
}

static void
moving(void)
{
	struct mh_ambient_bank bank, saved;
	struct mh_env_sounds definitions;
	struct mh_scene scene;
	struct mh_scene_object objects[2];
	struct mh_scene_key keys[2];
	struct mh_sound_listener listener;
	struct mh_mixer mixer, before;
	struct mh_mixer *mixers[1];
	struct mh_sound_pool pool, before_pool;
	struct mh_effects_mixer effects;
	double eye[3] = {0, 0, 0}, target[3] = {0, 0, 1};
	unsigned char pcm[4] = {0, 64, 128, 255};
	float output[16];
	unsigned int i;

	memset(&scene, 0, sizeof(scene));
	memset(objects, 0, sizeof(objects));
	memset(keys, 0, sizeof(keys));
	memset(&definitions, 0, sizeof(definitions));
	scene.objects = objects;
	scene.object_count = 2;
	objects[0].sound_class = objects[1].sound_class = 11;
	objects[0].keys = objects[1].keys = keys;
	objects[0].key_count = objects[1].key_count = 2;
	objects[0].end_behavior = objects[1].end_behavior = 2;
	keys[0].value[0] = -10;
	keys[1].value[0] = 10;
	keys[1].time = 2;
	keys[1].linear = 1;
	definitions.count = 1;
	definitions.sounds[0].kind = MH_ENV_OBJECT;
	definitions.sounds[0].object_class = 11;
	definitions.sounds[0].volume = 1;
	definitions.sounds[0].near_distance = 20;
	definitions.sounds[0].far_distance = 100;
	CHECK(mh_ambient_bind(&definitions, &scene, &bank, NULL) ==
			MH_ERROR_NONE &&
		bank.count == 2);
	CHECK(bank.motion[0] == &objects[0] && bank.motion[1] == &objects[1]);

	for (i = 0; i < 2; i++) {
		bank.waves[i].pcm = pcm;
		bank.waves[i].frames = bank.waves[i].bytes = 4;
		bank.waves[i].rate = 11025;
		bank.waves[i].channels = 1;
		bank.waves[i].bits = 8;
	}

	mh_sound_listener_camera(eye, target, &listener);
	CHECK(mh_mixer_init(48000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mixer.voices[0].pitch == 2 &&
		mixer.voices[0].left > mixer.voices[0].right);
	CHECK(mh_mixer_render(&mixer, output, 4, 0, NULL) == MH_ERROR_NONE);
	before = mixer;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 1.5, NULL) ==
		MH_ERROR_NONE);
	CHECK(mixer.voices[0].left < mixer.voices[0].right &&
		mixer.voices[0].cursor == before.voices[0].cursor);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 2, NULL) ==
			MH_ERROR_NONE &&
		mixer.voices[0].left > mixer.voices[0].right);
	before = mixer;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, -1, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&before, &mixer, sizeof(mixer)) == 0);
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(48000, 1, &mixer, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&mixer, &pool, 0, NULL) == MH_ERROR_NONE);
	mixers[0] = &mixer;
	CHECK(mh_effects_init(48000, 1, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].left > effects.pcm.voices[0].right);
	before = mixer;
	before_pool = pool;
	CHECK(mh_ambient_positions(&mixer, &bank, &listener, 1.5, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(mixer.voices, before.voices, sizeof(mixer.voices)) == 0 &&
		memcmp(&pool, &before_pool, sizeof(pool)) == 0);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].left < effects.pcm.voices[0].right);
	before = mixer;
	bank.motion[1] = NULL;
	CHECK(mh_ambient_positions(&mixer, &bank, &listener, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&mixer, &before, sizeof(mixer)) == 0);
	definitions.sounds[0].object_class = 16;
	CHECK(mh_ambient_bind(&definitions, &scene, &bank, NULL) ==
			MH_ERROR_NONE &&
		bank.count == 0);
	definitions.sounds[0].object_class = 0;
	objects[0].sound_class = 0;
	CHECK(mh_ambient_bind(&definitions, &scene, &bank, NULL) ==
			MH_ERROR_NONE &&
		bank.count == 0);
	definitions.sounds[0].object_class = 11;
	objects[0].sound_class = 11;
	definitions.count = MH_MIXER_VOICES / 2 + 1;

	for (i = 1; i < definitions.count; i++)
		definitions.sounds[i] = definitions.sounds[0];

	saved = bank;
	CHECK(mh_ambient_bind(&definitions, &scene, &bank, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&bank, &saved, sizeof(bank)) == 0);
}

static void
setup_fixture(struct mh_ambient_bank *bank, struct mh_mixer *mixer,
	struct mh_sound_pool *pool, struct mh_effects_mixer *effects,
	unsigned int count)
{
	static unsigned char pcm[8192];
	unsigned int i;

	memset(bank, 0, sizeof(*bank));
	bank->count = count;

	for (i = 0; i < count; i++) {
		bank->sounds[i].pitch = bank->sounds[i].volume = 1;
		bank->sounds[i].far_distance = 100;
		bank->sounds[i].position[2] = 200;
		bank->sounds[i].priority = 1;
		bank->waves[i].pcm = pcm;
		bank->waves[i].bytes = sizeof(pcm);
		bank->waves[i].frames = sizeof(pcm) / 2;
		bank->waves[i].channels = 1;
		bank->waves[i].bits = 16;
		bank->waves[i].rate = 22050;
	}

	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, pool, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_init(22050, 1, mixer, NULL) == MH_ERROR_NONE &&
		mh_mixer_bind(mixer, pool, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_init(22050, 1, effects, NULL) == MH_ERROR_NONE);
}

static void
eligibility(void)
{
	struct mh_ambient_bank bank;
	struct mh_mixer mixer, *mixers[1];
	struct mh_sound_pool pool;
	struct mh_effects_mixer effects;
	struct mh_sound_listener listener;
	struct mh_sound_spatial_result result, before;
	double source[3] = {0, 0, 10};
	float pcm[16];
	unsigned int i;

	memset(&listener, 0, sizeof(listener));
	listener.right[0] = listener.forward[2] = 1;
	CHECK(mh_sound_spatial_prepare(&listener, source, 0, 10, 100, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.eligible && result.gains[0] == 0 && result.gains[1] == 0);
	before = result;
	source[1] = 1e7;
	CHECK(mh_sound_spatial_prepare(&listener, source, 0, 10, 100, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &result, sizeof(result)) == 0);
	setup_fixture(&bank, &mixer, &pool, &effects, 9);
	mixers[0] = &mixer;

	for (i = 0; i < 8; i++) {
		bank.sounds[i].position[2] = 10;
		bank.sounds[i].volume = 0;
		bank.sounds[i].priority = 4;
	}

	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.pending[8] == 9 && pool.records[8].active &&
		!pool.records[8].request.eligible);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(pool.channels[i] == i + 1 &&
			effects.pcm.voices[i].active);

	CHECK(!pool.records[8].active);
	CHECK(mh_effects_render(&effects, &pool, mixers, 1, pcm, 8, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 16; i++)
		CHECK(pcm[i] == 0);

	bank.sounds[0].position[2] = 200;
	bank.sounds[8].position[2] = 10;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[0].active && pool.channels[0] == 1 &&
		effects.pcm.voices[0].active);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);

	/*
	 * The new quiet source loses to the old cached score before the
	 * now-out-of-range resident is removed in the same callback.
	 */
	CHECK(pool.channels[0] == 0 && !pool.records[8].active &&
		!effects.pcm.voices[0].active);
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
			MH_ERROR_NONE &&
		pool.channels[0] == 9);
}

static void
refresh_failure(void)
{
	struct mh_ambient_bank bank;
	struct mh_mixer mixer, before_mixer, *mixers[1];
	struct mh_sound_pool pool, before_pool;
	struct mh_effects_mixer effects, before_effects;
	struct mh_sound_listener listener;

	memset(&listener, 0, sizeof(listener));
	listener.right[0] = listener.forward[2] = 1;
	setup_fixture(&bank, &mixer, &pool, &effects, 2);
	mixers[0] = &mixer;
	bank.sounds[0].position[2] = bank.sounds[1].position[2] = 10;
	CHECK(mh_ambient_update(&mixer, &bank, &listener, 0, NULL) ==
		MH_ERROR_NONE);
	mixer.spatial[1].position[0] = 1e7;
	before_mixer = mixer;
	before_pool = pool;
	before_effects = effects;
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&mixer, &before_mixer, sizeof(mixer)) == 0 &&
		memcmp(&pool, &before_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &before_effects, sizeof(effects)) == 0);
	mixer.spatial[1].position[0] = 0;
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
		MH_ERROR_NONE);
	mixer.spatial[1].far_distance = 0;
	before_mixer = mixer;
	before_pool = pool;
	before_effects = effects;
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&mixer, &before_mixer, sizeof(mixer)) == 0 &&
		memcmp(&pool, &before_pool, sizeof(pool)) == 0 &&
		memcmp(&effects, &before_effects, sizeof(effects)) == 0);

	/* Physical completion short-circuits positional preparation. */
	effects.pcm.voices[pool.records[1].channel - 1].active = 0;
	CHECK(mh_effects_update(&effects, &pool, mixers, 1, NULL) ==
			MH_ERROR_NONE &&
		!pool.records[1].active && pool.records[0].active);
}

int
main(void)
{

	parser();
	spatial();
	playback();
	moving();
	eligibility();
	refresh_failure();

	return failures != 0;
}
