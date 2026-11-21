#include <stdio.h>
#include <string.h>

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
static struct mh_surface_bank bank;
static struct mh_field field;
static unsigned char pcm[64];

static int
check_effects(void)
{
	struct mh_body_effects effects, before;
	struct mh_body_sparks sparks;
	struct mh_body_result body;

	memset(&effects, 0, sizeof(effects));
	CHECK(mh_body_effect_impact(&effects, 0, 0, 1000, .5, 20, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.impulse == 1000 && effects.effect_material[0] == 0 &&
		effects.scrape_speed[0] == 10);
	CHECK(mh_body_effect_impact(&effects, 1, 3, -500, 1, 100, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.impulse == 1000 && effects.effect_material[0] == 0 &&
		effects.effect_material[1] == 1);
	CHECK(effects.scrape_speed[1] == 100 && effects.material == 1);

	/*
	 * A new maximum promotes the OLD primary, replacing the faster
	 * secondary.
	 */
	CHECK(mh_body_effect_impact(&effects, 2, 2, 1500, -.25, 40, NULL) ==
		MH_ERROR_NONE);
	CHECK(effects.impulse == 1500 && effects.effect_material[0] == 2 &&
		effects.effect_material[1] == 0 &&
		effects.scrape_speed[1] == 10);
	CHECK(mh_body_effect_contact(&effects, 3, NULL) == MH_ERROR_NONE &&
		effects.material == 2);
	before = effects;
	CHECK(mh_body_effect_impact(&effects, 1, 7, 500, 1, 10, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&before, &effects, sizeof(effects)) == 0);
	memset(&bank, 0, sizeof(bank));
	bank.materials.count = 3;
	bank.materials.entries[2].spark_anim = 7;
	CHECK(mh_body_sparks(&effects, &bank.materials, &sparks, NULL) ==
			MH_ERROR_NONE &&
		sparks.id[0] == 7);
	CHECK(sparks.gain[0] == (double)(float).3 && sparks.id[1] == 0);
	effects.scrape_speed[0] = -10;
	CHECK(mh_body_sparks(&effects, &bank.materials, &sparks, NULL) ==
			MH_ERROR_NONE &&
		sparks.id[0] == 0 && sparks.gain[0] < 0);
	memset(&body, 0, sizeof(body));
	memset(&effects, 0, sizeof(effects));
	body.impact_count = 2;
	body.impacts[0].contact.material = 1;
	body.impacts[0].corner = 0;
	body.impacts[0].impulse = 5000;
	body.impacts[0].normal_x = 1;
	body.impacts[0].forward = 40;
	body.impacts[1].contact.material = 2;
	body.impacts[1].corner = 3;
	body.impacts[1].impulse = 4000;
	body.impacts[1].normal_x = .5;
	body.impacts[1].forward = 60;
	body.residual[0].hit = 1;
	body.residual[0].material = 0;
	CHECK(mh_body_accumulate(&body, &effects, NULL) == MH_ERROR_NONE &&
		effects.material == 2 && effects.impulse == 5000);
	CHECK(effects.effect_material[0] == 1 &&
		effects.effect_material[1] == 2);
	memset(&body, 0, sizeof(body));
	CHECK(mh_body_accumulate(&body, &effects, NULL) == MH_ERROR_NONE &&
		effects.material == 2 && effects.impulse == 5000);
	body.impact_count = 1;
	body.impacts[0].impulse = 100;
	body.impacts[0].normal_x = 1;
	body.impacts[0].forward = 70;
	CHECK(mh_body_accumulate(&body, &effects, NULL) == MH_ERROR_NONE &&
		effects.impulse == 5000 && effects.material == 0 &&
		effects.scrape_speed[1] == 70);
	before = effects;
	body.impact_count = 4;
	CHECK(mh_body_accumulate(&body, &effects, NULL) != MH_ERROR_NONE &&
		memcmp(&before, &effects, sizeof(effects)) == 0);

	return 0;
}

static int
publish_body(void)
{
	field.cars[0].materials = &bank.materials;
	CHECK(mh_drive_body_publish(&field.cars[0], 1, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
playback(void)
{
	struct mh_surface_sound sound, before;
	struct mh_material_sound *definition;
	float output[128];
	double cursor;
	unsigned int i;

	memset(&bank, 0, sizeof(bank));
	memset(&field, 0, sizeof(field));
	field.listener_mask = 1;
	bank.count = 7;
	bank.materials.count = 2;
	bank.materials.entries[0].spark_anim = 1;

	for (i = 0; i < 7; i++) {
		bank.waves[i].pcm = pcm;
		bank.waves[i].bytes = 64;
		bank.waves[i].frames = 32;
		bank.waves[i].rate = 48000;
		bank.waves[i].channels = 1;
		bank.waves[i].bits = 16;
	}

	for (i = MH_MATERIAL_WEAK; i <= MH_MATERIAL_SCRATCH; i++) {
		definition = &bank.materials.entries[0].sounds[i];
		strcpy(definition->sample, "fixture.wav");
		definition->volume = 2;
		definition->pitch = 1;
		definition->loop_start = 8;
		definition->loop_tail = 8;
		bank.sample[0][i] = i + 3;
	}

	field.count = 1;
	field.cars[0].ticks = 1;
	CHECK(mh_body_effect_impact(&field.cars[0].body_effects, 0, 0, 1000, .5,
		20, NULL) == MH_ERROR_NONE);
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_init(48000, 1, &sound, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[6].active && sound.mixer.voices[7].active &&
		!sound.mixer.voices[15].active);
	CHECK(sound.mixer.voices[6].loop_start == 8 &&
		sound.mixer.voices[6].loop_end == 24);
	CHECK(sound.mixer.voices[7].left > sound.mixer.voices[7].right);
	CHECK(mh_mixer_render(&sound.mixer, output, 25, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[7].cursor == 9);
	cursor = sound.mixer.voices[6].cursor;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[6].cursor == cursor);
	field.cars[0].ticks++;
	CHECK(mh_body_effect_impact(&field.cars[0].body_effects, 0, 1, 16500, 1,
		50, NULL) == MH_ERROR_NONE);
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[6].active &&
		sound.mixer.voices[6].loop_end == 0);
	CHECK(sound.mixer.voices[7].right == 1 &&
		sound.mixer.voices[7].left < 1);
	CHECK(sound.mixer.voices[15].active &&
		sound.mixer.voices[15].cursor == 0 &&
		sound.mixer.voices[15].loop_end == 0);
	CHECK(mh_mixer_render(&sound.mixer, output, 5, 0, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Hard body contacts explicitly stop/copy/requeue on every sound pass,
	 * even with the same tick and feedback. Weak/scratch loops keep
	 * cursors.
	 */
	cursor = sound.mixer.voices[7].cursor;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		sound.mixer.voices[15].cursor == 0 &&
		sound.mixer.voices[7].cursor == cursor);

	/*
	 * Recovery clears the published impulse while retaining the physics
	 * maximum and spark publication. Audio must not reconstruct the old
	 * hard impact.
	 */
	CHECK(mh_mixer_render(&sound.mixer, output, 5, 0, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].body_impulse = 0;
	cursor = sound.mixer.voices[7].cursor;
	CHECK(field.cars[0].body_effects.impulse == 16500);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[15].cursor == 5 &&
		sound.mixer.voices[7].cursor == cursor);
	CHECK(sound.mixer.voices[7].loop_end == 24);
	before = sound;
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 1, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);

	/*
	 * Feedback can change without a local physics tick (e.g. viewed state).
	 * Loop-to-tail transitions must observe the supplied state immediately.
	 */
	memset(&field.cars[0].body_effects, 0,
		sizeof(field.cars[0].body_effects));
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[7].active &&
		sound.mixer.voices[7].loop_end == 0);

	/*
	 * Contact can return during the non-looping tail, beyond the old end.
	 */
	sound.mixer.voices[7].cursor = 27;
	field.cars[0].ticks++;
	CHECK(mh_body_effect_impact(&field.cars[0].body_effects, 0, 0, 1000, 1,
		20, NULL) == MH_ERROR_NONE);
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.mixer.voices[7].loop_end == 24 &&
		sound.mixer.voices[7].cursor == 11);
	field.cars[0].ticks++;
	memset(&field.cars[0].body_effects, 0,
		sizeof(field.cars[0].body_effects));
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_render(&sound.mixer, output, 64, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sound.mixer.voices[6].active && !sound.mixer.voices[7].active &&
		!sound.mixer.voices[15].active);

	/* Reverse scrape metric disables scratch but keeps impact playback. */
	field.cars[0].ticks++;
	CHECK(mh_body_effect_impact(&field.cars[0].body_effects, 0, 3, 1000, 1,
		-10, NULL) == MH_ERROR_NONE);
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		!sound.mixer.voices[7].active);
	CHECK(sound.mixer.voices[6].active);
	before = sound;
	field.cars[0].ticks++;
	field.cars[0].body_effects.material = 2;
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&sound, &before, sizeof(sound)) == 0);

	/*
	 * Missing material sound keeps the old record; current gain is still
	 * zero.
	 */
	field.cars[0].body_effects.material = 1;
	CHECK(publish_body() == 0);
	CHECK(mh_surface_sound_update(&sound, &bank, &field, 1, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(sound.body_bound[0] == 0 &&
		sound.mixer.voices[6].wave == &bank.waves[4] &&
		sound.mixer.voices[6].left == 0);

	return 0;
}

int
main(void)
{
	return check_effects() || playback();
}
