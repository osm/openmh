#include <stdio.h>
#include <string.h>

#include "engine/assets/material.h"
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
static struct mh_materials materials, saved;
static unsigned char pcm[80000];

static int
parsing(void)
{
	static const char good[] =
		"Materials 1\nMaterialName Road\nSndContact Asfalt2.wav 1 2 .13 .75 0 0\n"
		"SndSpin SPIN.WAV 1 3 3 1 36102 0\nSndScratch Metspark2.wav 1 3 1 1 7761 20109\nSlideType 1\n"
		"SparkAnimID 1\nContactAnimID 2\nSkidMark 3\n";
	static const char srf[] =
		"Materials 2\nMaterial missing\nMaterial road\n";
	static const char *bad[] = {"SndContact ../a.wav 1 2 1 1 0 0",
		"SndContact a.wav 1 2 1 1 0",
		"SndContact a.wav 1 2 1 1 0 0 extra",
		"SndContact a.wav 1 2 1 1 -1 0",
		"SndContact a.wav 1 2 1 1 0 0\nSndContact b.wav 1 2 1 1 0 0",
		"SndSpin a.wav 1 2 1 0 0 0", "SndSpin a.wav 1 2 nan 1 0 0",
		"SlideType 1\nSlideType 2", "SlideType 1.5"};

	char text[512];
	unsigned int i;

	CHECK(mh_materials_mat_srf_decode((const unsigned char *)good,
		strlen(good), (const unsigned char *)srf, strlen(srf),
		&materials, NULL) == MH_ERROR_NONE);
	CHECK(materials.count == 2 &&
		materials.entries[0].sounds[0].sample[0] == 0);
	CHECK(materials.entries[0].slide_type == 0 &&
		materials.entries[1].slide_type == 1);
	CHECK(materials.entries[1].spark_anim == 1 &&
		materials.entries[1].contact_anim == 2 &&
		materials.entries[1].skid_mark == 3);
	CHECK(strcmp(materials.entries[1].sounds[0].sample, "asfalt2.wav") ==
		0);
	CHECK(materials.entries[1].sounds[0].volume == (double)(float).13);
	CHECK(materials.entries[1].sounds[MH_MATERIAL_SPIN].loop_start ==
		36102);
	CHECK(materials.entries[1].sounds[MH_MATERIAL_SCRATCH].loop_tail ==
		20109);
	saved = materials;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		sprintf(text, "Materials 1\nMaterialName Road\n%s\n", bad[i]);
		CHECK(mh_materials_mat_srf_decode((const unsigned char *)text,
			strlen(text), (const unsigned char *)srf,
			strlen(srf), &materials, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&materials, &saved, sizeof(materials)) == 0);
	}

	return 0;
}

static int
playback(void)
{
	struct mh_surface_sound sound, before;
	struct mh_surface_parameters parameters;
	struct mh_material_sound *definition;
	float output[512];
	size_t loop_start, loop_end;
	unsigned int i, j;
	double cursor;

	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;

	for (i = 0; i < 8; i++)
		field.cars[i].vehicle.motion.rotation[0] =
			field.cars[i].vehicle.motion.rotation[4] =
				field.cars[i].vehicle.motion.rotation[8] = 1;

	bank.count = 7;
	bank.materials.count = 3;

	for (i = 0; i < bank.count; i++) {
		bank.waves[i].pcm = pcm;
		bank.waves[i].bytes = sizeof(pcm);
		bank.waves[i].frames = sizeof(pcm) / 2;
		bank.waves[i].rate = 48000;
		bank.waves[i].bits = 16;
		bank.waves[i].channels = 1;
	}

	for (i = 0; i < 2; i++)
		for (j = 0; j < MH_MATERIAL_SOUNDS; j++) {
			definition = &bank.materials.entries[i].sounds[j];
			strcpy(definition->sample, "fixture.wav");
			definition->flags = 1;
			definition->priority = i + 1;
			definition->volume = i == 0 ? .5 : .8;
			definition->pitch = .75;
			bank.sample[i][j] = 4 + i;
		}

	bank.materials.entries[0].slide_type = 1;
	field.count = 2;
	field.cars[0].target[2] = 1;
	field.cars[1].vehicle.motion.position[2] = 15;
	field.cars[0].camera.rotation[0] = field.cars[0].camera.rotation[4] =
		field.cars[0].camera.rotation[8] = 1;
	field.cars[0].vehicle.grip.wheels[0].supported = 1;
	field.cars[0].vehicle.motion.velocity[2] = 20;
	CHECK(mh_surface_sound_init(48000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.material[0] == 0 && sound.mixer.voices[0].active &&
		!sound.mixer.voices[3].active);
	CHECK(sound.mixer.voices[0].wave == &bank.waves[4]);
	CHECK(mh_mixer_render(&sound.mixer, output, 256, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = sound.mixer.voices[0].cursor;
	CHECK(cursor > 0 &&
		mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == cursor);
	field.cars[0].vehicle.grip.sliding = 1;
	field.cars[0].vehicle.grip.slide_amount = 4;
	field.cars[1].vehicle.grip.sliding = 1;
	field.cars[1].vehicle.grip.slide_amount = 2;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[3].active &&
		sound.mixer.voices[3].loop_start == 9952);
	CHECK(sound.mixer.voices[3].cursor == 0 &&
		sound.mixer.voices[8].active);
	CHECK(sound.mixer.voices[8].wave == &bank.waves[3] &&
		sound.mixer.voices[8].loop_start == 36102);
	CHECK(sound.mixer.voices[8].right > 0 &&
		sound.mixer.voices[8].pitch == (double)(float).93);

	/*
	 * Unavailable branch stops local tyre/body and remote skids, preserves
	 * cached material bindings, and lets an existing crash finish.
	 */
	before = sound;
	field.unavailable_mask = 3;
	CHECK(mh_mixer_play(&sound.mixer, 16, &bank.waves[4], 1, 1, 1, 0, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_play(&sound.mixer, 15, &bank.waves[4], 1, 1, 1, 0, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 16; i++)
		CHECK(!sound.mixer.voices[i].active);

	CHECK(sound.mixer.voices[16].active && sound.random == before.random);
	CHECK(memcmp(sound.material, before.material, sizeof(sound.material)) ==
			0 &&
		memcmp(sound.bound, before.bound, sizeof(sound.bound)) == 0);
	field.unavailable_mask = 0;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].active && sound.mixer.voices[3].active &&
		sound.mixer.voices[8].active);
	field.cars[0].vehicle.grip.sliding = 0;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sound.mixer.voices[3].active && sound.mixer.voices[4].active &&
		!sound.mixer.voices[5].active);
	CHECK(mh_mixer_render(&sound.mixer, output, 256, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = sound.mixer.voices[4].cursor;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[4].cursor == cursor);
	field.cars[0].vehicle.grip.sliding = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[4].active);
	field.cars[0].vehicle.heavy_brake = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[1].active &&
		sound.mixer.voices[1].pitch == (double)(float)1.08);
	CHECK(!sound.mixer.voices[3].active && sound.mixer.voices[5].active);
	before = sound;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	field.cars[0].vehicle.heavy_brake = 0;
	field.cars[0].vehicle.grip.wheels[1].supported = 1;
	field.cars[0].vehicle.grip.wheels[1].material = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.material[0] == 1 && sound.mixer.voices[2].active &&
		!sound.mixer.voices[3].active);
	CHECK(sound.mixer.voices[0].wave == &bank.waves[5] &&
		sound.mixer.voices[0].cursor == 0);

	/*
	 * Highest priority wins; equal priority preserves the earliest wheel.
	 */
	bank.materials.entries[1].sounds[0].priority = 1;
	CHECK(mh_surface_parameters(&bank.materials, &field.cars[0].vehicle, 0,
		&parameters, NULL) == MH_ERROR_NONE &&
		parameters.material == 0);
	field.cars[0].vehicle.grip.wheels[0].supported = 0;
	field.cars[0].vehicle.grip.wheels[1].material = 2;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.bound[0] == 1);
	CHECK(sound.mixer.voices[0].wave == &bank.waves[5] &&
		sound.mixer.voices[0].left == 0);
	before = sound;
	field.cars[0].vehicle.grip.wheels[1].material = 3;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);
	field.cars[0].vehicle.grip.wheels[1].supported = 0;
	field.cars[1].vehicle.wheelspin = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		CHECK(!sound.mixer.voices[i].active);

	CHECK(!sound.mixer.voices[8].active);
	field.cars[1].vehicle.wheelspin = 0;
	field.network = 1;
	field.active_mask = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[8].active);
	field.network = 0;
	field.cars[1].vehicle.motion.position[2] = 150;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[8].active);

	/*
	 * Detailed tyre/body ownership moves, positional skids keep field
	 * slots.
	 */
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	field.count = 2;
	field.active_mask = 3;

	for (i = 0; i < 2; i++)
		field.cars[i].vehicle.motion.rotation[0] =
			field.cars[i].vehicle.motion.rotation[4] =
				field.cars[i].vehicle.motion.rotation[8] = 1;

	field.cars[0].target[2] = 1;
	field.cars[0].vehicle.motion.position[2] = 15;
	field.cars[0].camera.rotation[0] = field.cars[0].camera.rotation[4] =
		field.cars[0].camera.rotation[8] = 1;
	field.cars[0].vehicle.grip.sliding = 1;
	field.cars[0].vehicle.grip.slide_amount = 2;
	field.cars[1].vehicle.grip.wheels[0].supported = 1;
	field.cars[1].vehicle.motion.velocity[2] = 20;
	field.cars[1].vehicle.grip.sliding = 1;
	field.cars[1].vehicle.grip.slide_amount = 4;
	CHECK(mh_surface_sound_init(48000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].active &&
		sound.mixer.voices[0].wave == &bank.waves[4]);
	CHECK(sound.mixer.voices[3].active && sound.mixer.voices[17].active);
	CHECK(!sound.mixer.voices[8].active);
	CHECK(mh_mixer_render(&sound.mixer, output, 256, 0, NULL) ==
		MH_ERROR_NONE);
	cursor = sound.mixer.voices[0].cursor;
	CHECK(cursor > 0 &&
		mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == cursor);
	field.cars[1].camera_mode = MH_CAMERA_TRACK;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == cursor &&
		sound.mixer.voices[3].active);
	CHECK(!sound.mixer.voices[8].active);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_CHASE, NULL) ==
		MH_ERROR_NONE);
	before = sound;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&before, &sound, sizeof(sound)) == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[0].cursor == 0 &&
		sound.mixer.voices[17].cursor == 0);
	CHECK(mh_field_camera_mode(&field, MH_CAMERA_TRACK, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sound.mixer.voices[0].active && !sound.mixer.voices[3].active);
	CHECK(sound.mixer.voices[8].active && sound.mixer.voices[17].active);

	/*
	 * Pitch the unviewed car so its display origin crosses the 100-unit
	 * sound boundary while its internal physics origin remains inside.
	 */
	field.cars[0].vehicle.motion.position[2] = 99.9;
	field.cars[0].vehicle.motion.rotation[4] =
		field.cars[0].vehicle.motion.rotation[8] = 0;
	field.cars[0].vehicle.motion.rotation[5] = 1;
	field.cars[0].vehicle.motion.rotation[7] = -1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sound.mixer.voices[17].active);
	field.cars[0].vehicle.motion.rotation[5] = -1;
	field.cars[0].vehicle.motion.rotation[7] = 1;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[17].active);
	before = sound;
	field.cars[0].vehicle.motion.rotation[0] = 2;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&before, &sound, sizeof(sound)) == 0);
	CHECK(mh_surface_loop(&bank.waves[0], 10, 100, &loop_start, &loop_end,
		NULL) == MH_ERROR_NONE &&
		loop_start == 10 && loop_end == 39900);
	bank.waves[0].frames = 24343;
	CHECK(mh_surface_loop(&bank.waves[0], 7761, 20109, &loop_start,
		&loop_end, NULL) == MH_ERROR_NONE &&
		loop_start == 4234 && loop_end == 7761);
	CHECK(mh_surface_loop(&bank.waves[0], 24343, 0, &loop_start, &loop_end,
		NULL) == MH_ERROR_NONE &&
		loop_start == 0 && loop_end == 24343);

	return 0;
}

int
main(void)
{
	return parsing() || playback();
}
