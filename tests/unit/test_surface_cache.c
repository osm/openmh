#include <stdio.h>
#include <string.h>

#include "engine/audio/effects.h"
#include "engine/audio/surface.h"
#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_field field;
static struct mh_surface_bank bank;
static struct mh_surface_sound sound;
static struct mh_sound_pool pool;
static unsigned char pcm[80000];

static int
setup_fixture(void)
{
	struct mh_material_sound *definition;
	struct mh_audio_pcm wave;
	unsigned int i, j;
	static const unsigned int masks[4] = {63, 63, 21, 42};

	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	memset(&bank, 0, sizeof(bank));
	memset(&wave, 0, sizeof(wave));
	wave.pcm = pcm;
	wave.bytes = sizeof(pcm);
	wave.frames = sizeof(pcm) / 2;
	wave.rate = 48000;
	wave.bits = 16;
	wave.channels = 1;
	bank.count = 24;
	bank.materials.count = 4;

	for (i = 0; i < bank.count; i++)
		bank.waves[i] = wave;

	for (i = 0; i < 4; i++) {
		bank.materials.entries[i].spark_anim = 1;

		for (j = 0; j < MH_MATERIAL_SOUNDS; j++) {
			definition = &bank.materials.entries[i].sounds[j];

			if (masks[i] & (1U << j))
				strcpy(definition->sample, "fixture.wav");

			definition->flags = j == MH_MATERIAL_HARD ? 0 : 1;
			definition->priority = 2;
			definition->volume = .125 * (i + 1);
			definition->pitch = .75 + .125 * i;
			bank.sample[i][j] = i * 6 + j;
		}
	}

	field.count = 8;
	field.active_mask = 255;

	for (i = 0; i < field.count; i++) {
		field.cars[i].target[2] = 1;
		field.cars[i].camera.rotation[0] =
			field.cars[i].camera.rotation[4] =
				field.cars[i].camera.rotation[8] = 1;
		field.cars[i].vehicle.motion.rotation[0] =
			field.cars[i].vehicle.motion.rotation[4] =
				field.cars[i].vehicle.motion.rotation[8] = 1;
		field.cars[i].vehicle.motion.velocity[2] = 20;
		field.cars[i].vehicle.grip.slide_amount = 2;
	}

	CHECK(mh_surface_sound_init(48000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_sound_pool_init(MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&sound.mixer, &pool, 0, NULL) == MH_ERROR_NONE);

	return 0;
}

/*
 * command 1 selects detailed playback, 2 deselects it. View changes and
 * explicit commands stop playback on the next unpaused sound pass.
 */
static int
update_surface(unsigned int selected, int tyre, int body, unsigned int slip,
	unsigned int impulse, unsigned int scratch, unsigned int command,
	unsigned int unavailable, int paused)
{
	struct mh_drive *drive;
	unsigned int i;
	int positional;

	CHECK(selected < 8 && tyre >= -1 && tyre < 4 && body >= -1 && body < 4);
	positional = mh_field_listener_positional(&field);

	if (selected != field.viewed_entry || command != 0)
		field.listener_revision++;

	field.viewed_entry = selected;

	if (command != 0)
		positional = command == 2;

	field.listener_mask = positional ? 0 : 1U << selected;
	field.unavailable_mask = unavailable ? 1U << selected : 0;

	for (i = 0; i < 8; i++) {
		drive = &field.cars[i];
		drive->vehicle.grip.sliding = drive->vehicle.wheelspin =
			drive->vehicle.heavy_brake = 0;
	}

	drive = &field.cars[selected];

	if (!mh_field_listener_positional(&field)) {
		drive->vehicle.grip.sliding = slip == 1;
		drive->vehicle.wheelspin = slip == 2;
		drive->vehicle.heavy_brake = slip == 3;
	}

	drive->vehicle.grip.wheels[0].supported = tyre >= 0;
	drive->vehicle.grip.wheels[0].material =
		tyre >= 0 ? (unsigned long)tyre : 0;
	memset(&drive->body_effects, 0, sizeof(drive->body_effects));

	if (body >= 0) {
		CHECK(mh_body_effect_impact(&drive->body_effects,
			(unsigned long)body, 0, impulse, 1, 20,
			NULL) == MH_ERROR_NONE);

		if (!scratch)
			drive->body_effects.present[0] =
				drive->body_effects.present[1] = 0;
	}

	drive->materials = &bank.materials;
	mh_body_sparks_begin_frame(&drive->body_sparks);
	CHECK(mh_drive_body_publish(drive, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, paused, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static int
retained(void)
{
	struct mh_surface_sound saved;
	struct mh_sound_pool saved_pool;
	unsigned int i;

	if (setup_fixture())
		return 1;

	CHECK(update_surface(0, 0, 0, 1, 1000, 1, 1, 0, 0) == 0);
	CHECK(update_surface(1, 1, 1, 1, 1000, 1, 1, 0, 0) == 0);
	CHECK(sound.material[0] == 0 && sound.material[1] == 1);
	CHECK(sound.body_material[0] == 0 && sound.body_material[1] == 1);
	CHECK(update_surface(0, 0, 0, 1, 1000, 1, 1, 0, 0) == 0);

	for (i = 0; i < 3; i++)
		CHECK(sound.bound[i] == 1);

	CHECK(sound.body_bound[0] == 1 && sound.body_bound[1] == 1);
	CHECK(sound.mixer.voices[0].wave == &bank.waves[6]);
	CHECK(sound.mixer.voices[0].pitch == .875 &&
		sound.mixer.voices[0].left < .051);
	CHECK(sound.mixer.voices[6].wave == &bank.waves[7]);
	CHECK(sound.mixer.voices[7].wave == &bank.waves[9]);
	CHECK(update_surface(0, 0, 0, 1, 1000, 1, 0, 1, 0) == 0);
	CHECK(!sound.mixer.voices[0].active && !sound.mixer.voices[6].active);
	CHECK(sound.mixer.voices[2].active && sound.mixer.voices[7].active);
	CHECK(update_surface(0, 0, 0, 1, 1000, 1, 0, 0, 0) == 0);

	/*
	 * Listener commands are deferred while paused, including cache updates.
	 */
	saved = sound;
	saved_pool = pool;
	CHECK(update_surface(2, 2, 2, 1, 18000, 1, 1, 0, 1) == 0);
	CHECK(memcmp(&saved, &sound, sizeof(sound)) == 0 &&
		memcmp(&saved_pool, &pool, sizeof(pool)) == 0);

	/*
	 * A late malformed hard-hit sample rolls back cache changes, bindings,
	 * earlier tyre requests and the pending listener stop together.
	 */
	bank.waves[14].pcm = NULL;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &sound, sizeof(sound)) == 0 &&
		memcmp(&saved_pool, &pool, sizeof(pool)) == 0);
	bank.waves[14].pcm = pcm;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.material[2] == 2 && sound.body_material[2] == 2);
	CHECK(sound.bound[0] == 2 && sound.bound[1] == 2 &&
		sound.bound[2] == 1);
	CHECK(sound.body_bound[0] == 1 && sound.body_bound[1] == 1 &&
		sound.body_bound[2] == 2);

	return 0;
}

static int
history_validation(void)
{
	struct mh_surface_history history, saved;
	struct mh_surface_sound before;
	struct mh_sound_pool before_pool;
	unsigned int i;

	if (setup_fixture())
		return 1;

	memset(&history, 0, sizeof(history));
	history.active[5] = 2;
	before = sound;
	before_pool = pool;
	CHECK(mh_surface_history_restore(&sound, &history, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&before, &sound, sizeof(sound)) == 0 &&
		memcmp(&before_pool, &pool, sizeof(pool)) == 0);
	history.active[5] = 1;
	history.channel[5] = 9;
	CHECK(mh_surface_history_restore(&sound, &history, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&before, &sound, sizeof(sound)) == 0 &&
		memcmp(&before_pool, &pool, sizeof(pool)) == 0);

	for (i = 0; i < 6; i++) {
		history.active[i] = 1;
		history.channel[i] = i + 1;
	}

	CHECK(mh_surface_history_restore(&sound, &history, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_history_save(&sound, &saved, NULL) == MH_ERROR_NONE &&
		memcmp(&history, &saved, sizeof(history)) == 0);
	before = sound;
	before_pool = pool;
	CHECK(mh_surface_history_restore(&sound, &history, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&before, &sound, sizeof(sound)) == 0 &&
		memcmp(&before_pool, &pool, sizeof(pool)) == 0);

	/*
	 * Actual material consumers must retire stale channels before copying
	 * their new definitions, including a hard impact on its first pass.
	 */
	CHECK(update_surface(0, 0, 0, 2, 18000, 1, 0, 0, 0) == 0);
	CHECK(mh_surface_history_save(&sound, &saved, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++)
		CHECK(saved.channel[i] == 0);

	CHECK(sound.bound[0] == 0 && sound.body_bound[2] == 0);

	return 0;
}

int
main(void)
{
	return retained() || history_validation();
}
