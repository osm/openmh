#include <stdio.h>
#include <string.h>

#include "engine/audio/effects.h"
#include "engine/audio/menu_sound.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Frontend sound line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_sound_bank bank;
	struct mh_menu_horn_bank horns;
	struct mh_sound_pool pool, saved_pool;
	struct mh_effects_mixer effects;
	const struct mh_sound_record *first_horn;
	struct mh_mixer mixers[MH_MENU_AUDIO_BANKS], saved[MH_MENU_AUDIO_BANKS],
		*banks[MH_MENU_AUDIO_BANKS];
	unsigned char pcm[64];
	float output[128], frequency, gain;
	unsigned int i;

	memset(&bank, 0, sizeof(bank));
	memset(pcm, 64, sizeof(pcm));
	bank.config.count = 37;
	CHECK(mh_sound_pool_init(MH_MENU_AUDIO_BANKS * MH_MIXER_VOICES, &pool,
		NULL) == MH_ERROR_NONE &&
		mh_effects_init(22050, .25, &effects, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_MENU_AUDIO_BANKS; i++) {
		banks[i] = &mixers[i];
		CHECK(mh_mixer_init(22050, .25, banks[i], NULL) ==
				MH_ERROR_NONE &&
			mh_mixer_bind(banks[i], &pool, i * MH_MIXER_VOICES,
				NULL) == MH_ERROR_NONE);
	}

	for (i = 0; i < 37; i++) {
		bank.config.events[i + 1] = i + 1;
		bank.config.samples[i].volume = bank.config.samples[i].pitch =
			1;
		bank.waves[i].pcm = pcm;
		bank.waves[i].bytes = sizeof(pcm);
		bank.waves[i].frames = 32;
		bank.waves[i].rate = 22050;
		bank.waves[i].channels = 1;
		bank.waves[i].bits = 16;
	}

	bank.config.events[0] = 1;
	saved_pool = pool;
	memcpy(saved, mixers, sizeof(saved));
	CHECK(mh_menu_sound_play(banks, &bank, 0, NULL) == MH_ERROR_NONE &&
		mh_menu_sound_play(banks, &bank, 38, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(saved, mixers, sizeof(saved)) == 0);

	/* Same PCM, independent records; upper bank includes sample 37. */
	CHECK(mh_menu_sound_play(banks, &bank, 1, NULL) == MH_ERROR_NONE &&
		mh_menu_sound_play(banks, &bank, 37, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE &&
		pool.channels[0] == 1 && pool.channels[1] == 37);
	CHECK(pool.records[36].request.flags == 4 &&
		pool.records[36].request.priority == 0 &&
		pool.records[36].score == .5f);
	CHECK(mh_effects_render(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		output, 8, 0, NULL) == MH_ERROR_NONE &&
		output[0] > .25f);
	CHECK(mh_menu_sound_play(banks, &bank, 1, NULL) == MH_ERROR_NONE &&
		pool.channels[0] == 0);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE);
	CHECK(effects.pcm.voices[0].cursor == 0 &&
		effects.pcm.voices[1].cursor == 8);

	/* A failed retrigger must not stop a valid resident. */
	saved_pool = pool;
	memcpy(saved, mixers, sizeof(saved));
	bank.waves[0].pcm = NULL;
	CHECK(mh_menu_sound_play(banks, &bank, 1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(saved, mixers, sizeof(saved)) == 0);
	bank.waves[0].pcm = pcm;
	bank.config.samples[0].volume = 10;
	CHECK(mh_menu_sound_parameters(&bank.config.samples[0], 22050,
		&frequency, &gain, NULL) == MH_ERROR_NONE &&
		gain == 1 && frequency == 22050);
	bank.config.samples[0].volume = -10;
	CHECK(mh_menu_sound_play(banks, &bank, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE &&
		pool.records[0].active && pool.records[0].score == 0);
	CHECK(mh_effects_render(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		output, 64, 0, NULL) == MH_ERROR_NONE);

	/* Completion waits for the callback. */
	CHECK(pool.records[0].active && pool.records[36].active);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE &&
		!pool.records[0].active && !pool.records[36].active);
	frequency = 123;
	gain = 456;
	bank.config.samples[0].pitch = 0;
	CHECK(mh_menu_sound_parameters(&bank.config.samples[0], 22050,
		&frequency, &gain, NULL) != MH_ERROR_NONE &&
		frequency == 123 && gain == 456);
	memset(&horns, 0, sizeof(horns));
	horns.catalog.count = MH_HORNS;

	for (i = 0; i < MH_HORNS; i++) {
		horns.waves[i] = bank.waves[1];
		horns.catalog.horns[i].volume = .125;
		horns.catalog.horns[i].pitch = 1.25;
	}

	CHECK(mh_menu_horn_play(banks, &horns, 0, NULL) == MH_ERROR_NONE &&
		mh_menu_horn_play(banks, &horns, 127, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE);
	first_horn = &pool.records[MH_MENU_SOUND_BANKS * MH_MIXER_VOICES];
	CHECK(first_horn->request.flags == 4 &&
		first_horn->request.priority == 4 &&
		first_horn->score == 4.09375f &&
		pool.records[MH_MENU_SOUND_BANKS * MH_MIXER_VOICES + 127]
			.active);
	CHECK(mh_effects_render(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		output, 8, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_horn_play(banks, &horns, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE);
	CHECK(banks[2]->voices[0].cursor == 0 &&
		banks[MH_MENU_SOUND_BANKS + 127 / MH_MIXER_VOICES]
				->voices[127 % MH_MIXER_VOICES]
				.cursor == 10);
	saved_pool = pool;
	memcpy(saved, mixers, sizeof(saved));
	horns.catalog.horns[0].pitch = 0;
	CHECK(mh_menu_horn_play(banks, &horns, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&pool, &saved_pool, sizeof(pool)) == 0 &&
		memcmp(saved, mixers, sizeof(saved)) == 0);
	horns.waves[1].pcm = NULL;
	CHECK(mh_menu_horn_play(banks, &horns, 1, NULL) == MH_ERROR_NONE &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);

	/*
	 * Horn priority must displace a menu sound in the same eight channels.
	 */
	CHECK(mh_sound_pool_init(MH_MENU_AUDIO_BANKS * MH_MIXER_VOICES, &pool,
		NULL) == MH_ERROR_NONE &&
		mh_effects_init(22050, .25, &effects, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_MENU_AUDIO_BANKS; i++) {
		CHECK(mh_mixer_init(22050, .25, banks[i], NULL) ==
				MH_ERROR_NONE &&
			mh_mixer_bind(banks[i], &pool, i * MH_MIXER_VOICES,
				NULL) == MH_ERROR_NONE);
	}

	bank.config.samples[0].pitch = bank.config.samples[0].volume = 1;
	horns.catalog.horns[0].pitch = 1.25;

	for (i = 1; i <= 8; i++)
		CHECK(mh_menu_sound_play(banks, &bank, i, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(pool.records[i].active);

	CHECK(mh_menu_horn_play(banks, &horns, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE &&
		pool.records[MH_MENU_SOUND_BANKS * MH_MIXER_VOICES].active);
	CHECK(mh_menu_sound_play(banks, &bank, 9, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, banks, MH_MENU_AUDIO_BANKS,
		NULL) == MH_ERROR_NONE &&
		!pool.records[8].active &&
		pool.records[MH_MENU_SOUND_BANKS * MH_MIXER_VOICES].active);

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		CHECK(pool.channels[i] != 0);

	puts("Frontend sounds: independent records, upper bank, retrigger, completion, clamping and atomic errors passed");

	return 0;
}
