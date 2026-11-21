#include <stdio.h>
#include <string.h>

#include "engine/audio/effects.h"
#include "engine/audio/pause_sound.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects;
	struct mh_mixer mixers[4], saved_mixers[4], *banks[4];
	struct mh_pause_sound_bank menu;
	unsigned char pcm[16];
	float output[32];
	unsigned int byte_index, sample_index, bank_index, voice_index;
	unsigned int output_index;

	memset(&menu, 0, sizeof(menu));

	for (byte_index = 0; byte_index < sizeof(pcm); byte_index += 2) {
		pcm[byte_index] = 0;
		pcm[byte_index + 1] = 64;
	}

	for (sample_index = 0; sample_index < MH_PAUSE_SOUND_SAMPLES;
		sample_index++) {
		menu.waves[sample_index].pcm = pcm;
		menu.waves[sample_index].frames = 8;
		menu.waves[sample_index].bytes = sizeof(pcm);
		menu.waves[sample_index].rate = 22050;
		menu.waves[sample_index].bits = 16;
		menu.waves[sample_index].channels = 1;
	}

	CHECK(mh_sound_pool_init(4 * MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_init(22050, .125, &effects, NULL) == MH_ERROR_NONE);

	for (bank_index = 0; bank_index < 4; bank_index++) {
		CHECK(mh_pause_sound_init(22050, .125, &mixers[bank_index],
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mixer_bind(&mixers[bank_index], &pool,
			MH_MIXER_VOICES * bank_index,
			NULL) == MH_ERROR_NONE);
		banks[bank_index] = &mixers[bank_index];
	}

	/*
	 * Stopping the physical device leaves cached logical scores in place.
	 * A new menu request loses to those residents before completion clears
	 * them on the next callback, just like other shared-bank requests.
	 */
	for (voice_index = 0; voice_index < 8; voice_index++) {
		CHECK(mh_mixer_policy(banks[0], voice_index, 1, 7, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mixer_play(banks[0], voice_index, &menu.waves[0], 1, 1,
			1, 0, 8, NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_effects_update(&effects, &pool, banks, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, 4, output, 2, 0,
		NULL) == MH_ERROR_NONE);
	saved_pool = pool;
	memcpy(saved_mixers, mixers, sizeof(saved_mixers));
	mh_effects_stop_device(&effects);
	mh_effects_stop_device(NULL);
	CHECK(memcmp(&saved_pool, &pool, sizeof(pool)) == 0 &&
		memcmp(saved_mixers, mixers, sizeof(saved_mixers)) == 0);

	for (voice_index = 0; voice_index < 8; voice_index++)
		CHECK(!effects.pcm.voices[voice_index].active &&
			effects.pcm.voices[voice_index].cursor == 2);

	CHECK(mh_pause_sound_play(banks[3], &menu, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, 4, NULL) ==
		MH_ERROR_NONE);

	for (voice_index = 0; voice_index < 8; voice_index++)
		CHECK(pool.channels[voice_index] == 0);

	CHECK(!banks[3]->voices[0].active);
	CHECK(mh_pause_sound_play(banks[3], &menu, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_pause_sound_play(banks[3], &menu, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, 4, output, 2, 0,
		NULL) == MH_ERROR_NONE &&
		output[0] == .125f);
	saved_pool = pool;
	memcpy(saved_mixers, mixers, sizeof(saved_mixers));
	CHECK(mh_pause_sound_play(banks[3], &menu, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_pause_sound_play(banks[3], &menu, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved_pool, &pool, sizeof(pool)) == 0 &&
		memcmp(saved_mixers, mixers, sizeof(saved_mixers)) == 0);
	CHECK(mh_effects_render(&effects, &pool, banks, 4, output, 14, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(banks[3]->voices[0].active && !banks[3]->voices[1].active);
	CHECK(output[26] == .0625f); /* Only the menu loop remains. */
	menu.waves[2].pcm = NULL;
	saved_pool = pool;
	memcpy(saved_mixers, mixers, sizeof(saved_mixers));
	CHECK(mh_pause_sound_play(banks[3], &menu, 2, NULL) != MH_ERROR_NONE);
	CHECK(mh_pause_sound_play(banks[3], &menu, 4, NULL) != MH_ERROR_NONE);
	CHECK(mh_pause_sound_play(NULL, &menu, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved_pool, &pool, sizeof(pool)) == 0 &&
		memcmp(saved_mixers, mixers, sizeof(saved_mixers)) == 0);

	for (sample_index = 0; sample_index < MH_PAUSE_SOUND_SAMPLES;
		sample_index++)
		mh_mixer_stop(banks[3], sample_index);

	CHECK(mh_effects_update(&effects, &pool, banks, 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_render(&effects, &pool, banks, 4, output, 16, 0,
		NULL) == MH_ERROR_NONE);

	for (output_index = 0; output_index < 32; output_index++)
		CHECK(output[output_index] == 0);

	return failures != 0;
}
