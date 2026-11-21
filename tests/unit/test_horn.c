#include <stdio.h>
#include <string.h>

#include "engine/assets/horn.h"
#include "engine/assets/profile.h"
#include "engine/audio/horn.h"
#include "engine/audio/race_sound.h"
#include "engine/catalog/profile.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
parsing(void)
{
	struct mh_horn horn, before;
	struct mh_horn_catalog catalog;
	const char *valid =
		"// test\r\nNaMe  My\t Horn // label\nSampleName CarHorns\\Horn1.Wav\n"
		"VolumeChange -50\nPitchChange 50\nFutureOption ignored\n";
	const char *bad[] = {"SampleName ../a.wav", "SampleName a/../b",
		"SampleName /a.wav", "SampleName C:\\a.wav", "SampleName a\\",
		"SampleName a/./b", "Name", "PitchChange nan",
		"PitchChange -91", "PitchChange 301", "VolumeChange 701",
		"VolumeChange -101", "VolumeChange 1 2", "VolumeChange",
		"PitchChange inf", "SampleName"};

	char long_line[514];
	double gain, pitch;
	unsigned int i, index;

	CHECK(mh_horn_chf_decode((const unsigned char *)valid, strlen(valid), 0,
		&horn, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(horn.name, "My Horn") == 0 &&
		strcmp(horn.sample, "CarHorns\\Horn1.Wav") == 0);
	CHECK(horn.volume == .5 && horn.pitch == 1.5);
	CHECK(mh_horn_parameters(&horn, &gain, &pitch, NULL) == MH_ERROR_NONE &&
		gain == .375 && pitch == 2.25);
	before = horn;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(mh_horn_chf_decode((const unsigned char *)bad[i],
			strlen(bad[i]), 0, &horn, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&horn, &before, sizeof(horn)) == 0);
	}

	memset(long_line, 'a', sizeof(long_line));
	CHECK(mh_horn_chf_decode((const unsigned char *)long_line,
		sizeof(long_line), 0, &horn, NULL) != MH_ERROR_NONE);
	CHECK(mh_horn_chf_decode((const unsigned char *)"Name A\0B", 8, 0,
		&horn, NULL) != MH_ERROR_NONE);
	CHECK(mh_horn_chf_decode((const unsigned char *)"", 0, 9, &horn,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(horn.name, "Horn 09") == 0 &&
		strcmp(horn.sample, "<None>") == 0 && horn.volume == 1 &&
		horn.pitch == 1);
	CHECK(mh_horn_parameters(&horn, &gain, &pitch, NULL) == MH_ERROR_NONE &&
		gain == 1.5 && pitch == 1);
	valid = "Name abcdefghijklmnopqrstuvwxyz\nPitchChange -90\nVolumeChange -100";
	CHECK(mh_horn_chf_decode((const unsigned char *)valid, strlen(valid), 0,
		&horn, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(horn.name, "abcdefghijklmno") == 0);
	CHECK(mh_horn_parameters(&horn, &gain, &pitch, NULL) == MH_ERROR_NONE &&
		gain >= 0 && gain < 1e-30 && pitch >= .01);
	memset(&catalog, 0, sizeof(catalog));
	catalog.count = 2;
	strcpy(catalog.horns[0].name, "Cow");
	strcpy(catalog.horns[1].name, "Trainhorn");
	index = 99;
	CHECK(mh_horn_find(&catalog, "cOw", &index, NULL) == MH_ERROR_NONE &&
		index == 0);
	CHECK(mh_horn_find(&catalog, "Trainhorn", &index, NULL) ==
			MH_ERROR_NONE &&
		index == 1);
	CHECK(mh_horn_find(&catalog, "Horn14.wav", &index, NULL) !=
			MH_ERROR_NONE &&
		index == 1);
	strcpy(catalog.horns[1].name, "cow");
	CHECK(mh_horn_find(&catalog, "Cow", &index, NULL) != MH_ERROR_NONE &&
		index == 1);
	strcpy(catalog.horns[0].name, "None");
	strcpy(catalog.horns[1].name, "Honker1");
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_NONE &&
		index == 1);
	catalog.count = 1;
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_NONE &&
		index == 0);
	index = 99;
	catalog.count = 0;
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_INVALID &&
		index == 99);
	catalog.count = 2;
	strcpy(catalog.horns[1].name, "none");
	CHECK(mh_horn_default(&catalog, &index, NULL) == MH_ERROR_INVALID &&
		index == 99);

	horn.pitch = 0;
	gain = pitch = -5;
	CHECK(mh_horn_parameters(&horn, &gain, &pitch, NULL) != MH_ERROR_NONE &&
		gain == -5 && pitch == -5);
}

static void
playback(void)
{
	static struct mh_field field;
	struct mh_sound_bank bank;
	struct mh_race_sound sound, before;
	struct mh_audio_pcm wave;
	unsigned char pcm[128];
	float samples[256];
	unsigned int i;

	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	memset(&bank, 0, sizeof(bank));
	memset(&wave, 0, sizeof(wave));
	memset(pcm, 192, sizeof(pcm));
	wave.pcm = pcm;
	wave.frames = sizeof(pcm);
	wave.channels = 1;
	wave.bits = 8;
	wave.rate = 8000;

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		bank.waves[i] = wave;

	for (i = 0; i < 8; i++) {
		bank.horns[i].wave = wave;
		bank.horns[i].volume = 1.5;
		bank.horns[i].pitch = 1;
		field.cars[i].vehicle.engine.rpm = 1000;
		field.cars[i].vehicle.motion.rotation[0] =
			field.cars[i].vehicle.motion.rotation[4] =
				field.cars[i].vehicle.motion.rotation[8] = 1;
	}

	field.count = 2;
	field.network = 1;
	field.active_mask = 3;
	field.cars[0].target[2] = 1;
	field.cars[0].camera.rotation[0] = field.cars[0].camera.rotation[4] =
		field.cars[0].camera.rotation[8] = 1;
	field.cars[1].remote = 1;
	field.cars[1].vehicle.motion.position[2] = 5;
	CHECK(mh_race_sound_init(8000, 1, &sound, NULL) == MH_ERROR_NONE);
	field.cars[0].horn = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[20].active &&
		sound.mixer.voices[20].loop_end == 0);
	CHECK(sound.mixer.voices[20].left == 1 &&
		sound.mixer.voices[20].right == 1);
	CHECK(mh_mixer_render(&sound.mixer, samples, 16, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[20].cursor == 16);
	field.cars[0].horn = 0;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[20].active);
	before = sound;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	field.cars[0].horn = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[20].cursor == 16);
	CHECK(mh_mixer_render(&sound.mixer, samples, 128, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[20].active);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[20].cursor == 0);
	CHECK(sound.mixer.voices[20].active);
	field.cars[0].horn = 0;
	CHECK(mh_mixer_render(&sound.mixer, samples, 128, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[20].active);
	field.cars[1].horn = 1;
	bank.horns[1].pitch = 2.25;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[21].wave == &bank.horns[1].wave &&
		sound.mixer.voices[21].pitch == 2.25);
	CHECK(sound.mixer.voices[21].active &&
		sound.mixer.voices[21].left > .89 &&
		sound.mixer.voices[21].left < .91);
	CHECK(mh_mixer_render(&sound.mixer, samples, 4, 0, NULL) ==
		MH_ERROR_NONE);
	field.cars[1].horn = 0;
	field.cars[1].vehicle.motion.position[0] = 10;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[21].cursor == 9 &&
		sound.mixer.voices[21].right > sound.mixer.voices[21].left);
	field.cars[1].vehicle.motion.position[0] = 100;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[21].active);
	field.cars[1].vehicle.motion.position[0] = 0;
	field.cars[1].horn = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[21].active);
	field.active_mask = 1;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[21].active);
	field.cars[0].horn = 2;
	before = sound;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	field.cars[0].horn = 0;
	field.active_mask = 3;

	/*
	 * The audible boundary follows the car's display pose, including its
	 * rotated local-Y offset. AI and received network poses use one path.
	 */
	for (i = 0; i < 2; i++) {
		field.cars[1].remote = (int)i;
		field.cars[1].vehicle.motion.position[2] = 74.9;
		field.cars[1].vehicle.motion.rotation[4] =
			field.cars[1].vehicle.motion.rotation[8] = 0;
		field.cars[1].vehicle.motion.rotation[5] = 1;
		field.cars[1].vehicle.motion.rotation[7] = -1;
		CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(!sound.mixer.voices[21].active &&
			sound.mixer.voices[8].active);
		field.cars[1].vehicle.motion.rotation[5] = -1;
		field.cars[1].vehicle.motion.rotation[7] = 1;
		CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(sound.mixer.voices[21].active);
		field.cars[1].vehicle.motion.position[2] = 99.9;
		CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(sound.mixer.voices[8].active);
		field.cars[1].vehicle.motion.rotation[5] = 1;
		field.cars[1].vehicle.motion.rotation[7] = -1;
		CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(!sound.mixer.voices[8].active);
	}

	before = sound;
	field.cars[1].vehicle.motion.rotation[0] = 2;
	CHECK(mh_race_sound_update(&sound, &bank, &field, 0, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sound, &before, sizeof(sound)) == 0);
}

static void
profile_lookup(void)
{
	struct mh_horn_catalog catalog;
	char name[16];
	unsigned int index;
	unsigned long random;

	memset(&catalog, 0, sizeof(catalog));
	catalog.count = 2;
	strcpy(catalog.horns[0].name, "Honker1");
	strcpy(catalog.horns[1].name, "Honker1");
	strcpy(name, "HONKER1");
	index = 99;
	random = 123;
	CHECK(mh_horn_profile_find(&catalog, name, &random, &index, NULL) ==
		MH_ERROR_NONE);
	CHECK(index == 0 && random == 123);
	strcpy(name, "bad");
	index = 99;
	CHECK(mh_horn_profile_find(&catalog, name, &random, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(index == 99 && random == 123);
	CHECK(mh_profile_catalog_advance_rng(0, &random, NULL) ==
			MH_ERROR_NONE &&
		random == 123);
	CHECK(mh_profile_catalog_advance_rng(MH_PROFILE_CATALOG_LIMIT + 1,
		&random, NULL) != MH_ERROR_NONE &&
		random == 123);
	CHECK(mh_profile_catalog_advance_rng(26, NULL, NULL) != MH_ERROR_NONE);
	memset(name, 'x', sizeof(name));
	CHECK(mh_horn_profile_find(&catalog, name, &random, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(index == 99 && random == 123);
	strcpy(name, "Horn3");
	catalog.count = 0;
	CHECK(mh_horn_profile_find(&catalog, name, &random, &index, NULL) !=
		MH_ERROR_NONE);
	CHECK(index == 99 && random == 123);
}

int
main(void)
{

	parsing();
	playback();
	profile_lookup();

	return failures != 0;
}
