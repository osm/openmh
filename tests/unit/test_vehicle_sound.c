#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/effects.h"
#include "engine/audio/vehicle_sound.h"
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
static struct mh_route route;
static struct mh_sound_bank bank;
static struct mh_surface_bank materials;
static struct mh_race_sound race;
static struct mh_surface_sound surface;
static struct mh_sound_pool pool;
static struct mh_mixer foreign;
static unsigned char pcm[80000];

static int
setup_fixture(unsigned int count, unsigned int selected, unsigned int owners,
	unsigned int active, unsigned int unavailable, unsigned int horns,
	unsigned int slide, unsigned int spin, unsigned int brake,
	unsigned int tyre, unsigned int body, unsigned int impact,
	unsigned int fill, unsigned long seed, double rpm, double pedal,
	double blend)
{
	struct mh_audio_pcm wave;
	struct mh_material_sound *definition;
	struct mh_drive *drive;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(&bank, 0, sizeof(bank));
	memset(&materials, 0, sizeof(materials));
	memset(&wave, 0, sizeof(wave));
	wave.pcm = pcm;
	wave.bytes = sizeof(pcm);
	wave.frames = sizeof(pcm) / 2;
	wave.rate = 48000;
	wave.bits = 16;
	wave.channels = 1;

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		bank.waves[i] = wave;

	materials.count = 9;
	materials.materials.count = 1;
	materials.materials.entries[0].slide_type = tyre == 2;
	materials.materials.entries[0].spark_anim = 1;

	for (i = 0; i < materials.count; i++)
		materials.waves[i] = wave;

	for (i = 0; i < MH_MATERIAL_SOUNDS; i++) {
		definition = &materials.materials.entries[0].sounds[i];
		strcpy(definition->sample, "fixture.wav");
		definition->flags = i == MH_MATERIAL_HARD ? 0 : 1;
		definition->priority = 2;
		definition->volume = .5;
		definition->pitch = 1;
	}

	for (i = 0; i < 4; i++) {
		materials.crashes.samples[i].enabled = 1;
		materials.crashes.samples[i].wave = wave;
		materials.crashes.samples[i].pitch = 1;
		materials.crashes.samples[i].priority = 2;
	}

	field.count = count;
	field.active_mask = active;
	field.network = 1;
	field.viewed_entry = selected;
	field.listener_mask = owners;
	field.unavailable_mask = unavailable;

	for (i = 0; i < count; i++) {
		drive = &field.cars[i];
		drive->ticks = 1;
		drive->target[2] = 1;
		drive->camera.rotation[0] = drive->camera.rotation[4] =
			drive->camera.rotation[8] = 1;
		drive->vehicle.motion.rotation[0] =
			drive->vehicle.motion.rotation[4] =
				drive->vehicle.motion.rotation[8] = 1;
		drive->vehicle.motion.velocity[2] = 20;
		drive->vehicle.engine.rpm = rpm;
		drive->vehicle.engine.gear = 3;
		drive->config.options.manual = 1;
		drive->sound_pedal = pedal;
		drive->camera_mode = MH_CAMERA_IN_CAR;
		drive->route = drive->race.route = &route;
		drive->race.sound_blend = (float)blend;
		drive->horn = (horns >> i) & 1;
		bank.horns[i].wave = wave;
		bank.horns[i].volume = .5;
		bank.horns[i].pitch = 1;
		drive->vehicle.engine_braking = 1;
		drive->vehicle.grip.sliding = (slide >> i) & 1;
		drive->vehicle.grip.slide_amount = 2;
		drive->vehicle.wheelspin = (spin >> i) & 1;
		drive->vehicle.heavy_brake = (brake >> i) & 1;
		drive->vehicle.grip.wheels[0].supported = tyre != 0;

		if (body != 0)
			CHECK(mh_body_effect_impact(&drive->body_effects, 0, 0,
				body == 1 ? 1000 : 18000, 1, 20,
				NULL) == MH_ERROR_NONE);

		drive->materials = &materials.materials;
		CHECK(mh_drive_body_publish(drive, 1, NULL) == MH_ERROR_NONE);

		if (impact != 0)
			CHECK(mh_impact_add(&drive->impact_effects, 0,
				impact == 1 ? 15 : 60,
				NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_race_sound_init(48000, 1, &race, NULL) == MH_ERROR_NONE);
	CHECK(mh_surface_sound_init(48000, 1, &surface, NULL) == MH_ERROR_NONE);
	surface.random = seed;
	CHECK(mh_sound_pool_init(3 * MH_MIXER_VOICES, &pool, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&race.mixer, &pool, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&surface.mixer, &pool, MH_MIXER_VOICES, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mixer_init(48000, 1, &foreign, NULL) == MH_ERROR_NONE);
	CHECK(mh_mixer_bind(&foreign, &pool, 2 * MH_MIXER_VOICES, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < fill; i++)
		CHECK(mh_mixer_play(&foreign, i, &bank.waves[0], 1, 1, 1, 0, 0,
			NULL) == MH_ERROR_NONE);

	return 0;
}

static int
atomicity(void)
{
	struct mh_race_sound saved_race;
	struct mh_surface_sound saved_surface;
	struct mh_sound_pool saved_pool, separate;

	CHECK(setup_fixture(8, 7, 128, 255, 0, 255, 255, 0, 0, 2, 1, 1, 0, 13,
		9000, 0, .5) == 0);
	saved_race = race;
	saved_surface = surface;
	saved_pool = pool;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&surface, &saved_surface, sizeof(surface)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	materials.crashes.samples[1].wave.pcm = NULL;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&surface, &saved_surface, sizeof(surface)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	materials.crashes.samples[1].wave.pcm = pcm;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(race.mixer.pool == &pool && surface.mixer.pool == &pool);

	/* A malformed late car must also undo earlier cars in both banks. */
	saved_race = race;
	saved_surface = surface;
	saved_pool = pool;
	field.cars[7].horn = 2;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&surface, &saved_surface, sizeof(surface)) == 0 &&
		memcmp(&pool, &saved_pool, sizeof(pool)) == 0);
	field.cars[7].horn = 1;
	surface.mixer.pool = &separate;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) != MH_ERROR_NONE);
	surface.mixer.pool = &pool;
	CHECK(mh_vehicle_sound_update(NULL, NULL, &surface, &materials, &field,
		0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_update(&race, &bank, NULL, NULL, &field, 0, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_update(NULL, NULL, NULL, NULL, &field, 0, 0,
		NULL) == MH_ERROR_NONE);

	return 0;
}

static int
completion(void)
{
	struct mh_effects_mixer effects;
	struct mh_mixer *mixers[3];
	float output[8];
	double finished_cursor;
	unsigned int i, channel;

	CHECK(setup_fixture(1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 13, 4000, 1,
		0) == 0);
	field.cars[0].vehicle.engine_braking = 0;
	field.cars[0].config.options.manual = 0;

	for (i = 0; i < 4; i++) {
		materials.crashes.samples[i].wave.bytes = 8;
		materials.crashes.samples[i].wave.frames = 4;
	}

	mixers[0] = &race.mixer;
	mixers[1] = &surface.mixer;
	mixers[2] = &foreign;
	CHECK(mh_effects_init(48000, 1, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(surface.random == 326 && surface.crash.bound == 0);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[MH_MIXER_VOICES + 16].channel != 0);
	CHECK(mh_effects_render(&effects, &pool, mixers, 3, output, 4, 0,
		NULL) == MH_ERROR_NONE);

	/*
	 * Until the device refresh publishes completion, the logical record
	 * remains active. Repeating the sound pass must not restart it early.
	 */
	finished_cursor = surface.mixer.voices[16].cursor;
	CHECK(surface.mixer.voices[16].active && finished_cursor == 0);
	channel = pool.records[MH_MIXER_VOICES + 16].channel - 1;
	CHECK(!effects.pcm.voices[channel].active);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(surface.random == 326 &&
		surface.mixer.voices[16].cursor == finished_cursor);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
			MH_ERROR_NONE &&
		!surface.mixer.voices[16].active);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].ticks == 1 && surface.random == 639 &&
		surface.crash.bound == 1 && surface.mixer.voices[16].active &&
		surface.mixer.voices[16].cursor == 0);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[MH_MIXER_VOICES + 16].channel != 0);

	/*
	 * Clearing contact stops future requests but lets this sample finish.
	 */
	field.cars[0].impact_effects.contact = 0;
	CHECK(mh_effects_render(&effects, &pool, mixers, 3, output, 4, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);
	CHECK(!surface.mixer.voices[16].active && surface.random == 639);

	return 0;
}

static int
multiple_owners(void)
{
	struct mh_sound_parameters expected[MH_ENGINE_SAMPLES];
	unsigned int i;

	CHECK(setup_fixture(2, 0, 3, 3, 0, 3, 3, 0, 0, 1, 1, 0, 0, 13, 3000, 1,
		0) == 0);
	field.cars[0].vehicle.engine_braking =
		field.cars[1].vehicle.engine_braking = 0;
	field.cars[0].vehicle.engine.gear = 2;
	field.cars[1].vehicle.engine.gear = 4;
	field.cars[1].vehicle.engine.rpm = 9000;
	field.cars[1].sound_pedal = 0;
	field.cars[1].race.sound_blend = .75f;
	field.cars[1].camera_mode =
		MH_CAMERA_TRACK; /* The viewed camera controls both mixes. */
	materials.materials.count = 2;
	materials.materials.entries[1] = materials.materials.entries[0];

	for (i = 0; i < MH_MATERIAL_SOUNDS; i++)
		materials.sample[1][i] = 1;

	field.cars[1].vehicle.grip.wheels[0].material = 1;
	field.cars[1].body_effects.material =
		field.cars[1].body_effects.effect_material[0] = 1;
	CHECK(mh_drive_body_publish(&field.cars[1], 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_engine_sound(9000, 0, .75, 1, expected, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_ENGINE_SAMPLES; i++)
		if (race.mixer.voices[i].active) {
			CHECK(race.mixer.voices[i].pitch == expected[i].pitch);
			CHECK(race.mixer.voices[i].left == expected[i].left &&
				race.mixer.voices[i].right ==
					expected[i].right);
		}

	CHECK(race.gear[0] == 2 && race.gear[1] == 4);
	CHECK(race.mixer.flags[20] == 0 && race.mixer.flags[21] == 0);
	CHECK(surface.material[0] == 0 && surface.material[1] == 1);
	CHECK(surface.body_material[0] == 0 && surface.body_material[1] == 1);
	CHECK(surface.mixer.voices[0].wave == &materials.waves[1]);
	CHECK(surface.mixer.voices[6].wave == &materials.waves[1]);

	/*
	 * A later unavailable detailed car stops the shared records just
	 * requested by the local car, preserving material slide and scratch.
	 */
	field.unavailable_mask = 2;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 1, 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_ENGINE_SAMPLES; i++)
		CHECK(!race.mixer.voices[i].active);

	CHECK(!surface.mixer.voices[0].active &&
		!surface.mixer.voices[6].active);
	CHECK(surface.mixer.voices[2].active && surface.mixer.voices[7].active);

	return 0;
}

static int
positional(void)
{
	static const unsigned int ids[3] = {20, 15, MH_MIXER_VOICES + 17};
	struct mh_effects_mixer effects;
	struct mh_mixer *mixers[3];
	unsigned int i;

	CHECK(setup_fixture(1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 13, 3000, 0,
		0) == 0);
	CHECK(mh_effects_init(48000, 1, &effects, NULL) == MH_ERROR_NONE);
	mixers[0] = &race.mixer;
	mixers[1] = &surface.mixer;
	mixers[2] = &foreign;
	field.cars[0].vehicle.motion.position[2] = 200;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(pool.records[ids[i]].active &&
			!pool.records[ids[i]].request.eligible);

	CHECK(pool.pending[0] == ids[0] + 1 && pool.pending[1] == ids[1] + 1 &&
		pool.pending[2] == ids[2] + 1);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(!pool.records[ids[i]].active);

	field.cars[0].vehicle.motion.position[2] = 10;
	bank.horns[0].volume = 0;
	field.cars[0].vehicle.grip.slide_amount = -1;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(pool.records[ids[i]].active &&
			pool.records[ids[i]].channel != 0);

	CHECK(race.mixer.voices[20].left == 0 &&
		surface.mixer.voices[17].left == 0);
	field.cars[0].vehicle.motion.position[2] = 200;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(pool.records[ids[i]].active &&
			effects.pcm.voices[pool.records[ids[i]].channel - 1]
				.active);

	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(!pool.records[ids[i]].active);

	field.listener_mask = field.unavailable_mask = 1;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(pool.records[20].active && pool.records[20].request.eligible &&
		race.mixer.voices[20].left == 0);

	return 0;
}

static int
listener_pose(void)
{
	double horn_pitch, engine_pitch;

	CHECK(setup_fixture(1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 13, 3000, 0,
		0) == 0);

	/*
	 * Camera rolled 90 degrees: world +Y is its right. Keep legacy
	 * eye/target diagnostics different so they cannot silently replace the
	 * real pose.
	 */
	memset(field.cars[0].camera.rotation, 0,
		sizeof(field.cars[0].camera.rotation));
	field.cars[0].camera.rotation[1] = field.cars[0].camera.rotation[8] = 1;
	field.cars[0].camera.rotation[3] = -1;
	field.cars[0].camera.position[0] = 5;
	field.cars[0].camera.position[1] = 30;
	field.cars[0].camera.position[2] = 8;
	field.cars[0].vehicle.motion.position[0] = 5;
	field.cars[0].vehicle.motion.position[1] = 35 - .3199999928474426;
	field.cars[0].vehicle.motion.position[2] = 8;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(fabs(race.mixer.voices[20].left - .08) < 0.0000001);
	CHECK(fabs(race.mixer.voices[20].right - .4) < 0.0000001);
	CHECK(fabs(race.mixer.voices[15].left - .096) < 0.0000001);
	CHECK(fabs(race.mixer.voices[15].right - .48) < 0.0000001);
	CHECK(surface.mixer.voices[17].active &&
		surface.mixer.voices[17].left > 0 &&
		surface.mixer.voices[17].right >
			surface.mixer.voices[17].left * 4.99);
	horn_pitch = race.mixer.voices[20].pitch;
	engine_pitch = race.mixer.voices[15].pitch;
	field.cars[0].camera.rotation[1] = -1;
	field.cars[0].camera.rotation[3] = 1;
	field.cars[0].vehicle.motion.velocity[0] = 100;
	field.cars[0].vehicle.motion.velocity[1] = -100;
	CHECK(mh_race_sound_vehicles(&race, &bank, &field, 0, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_surface_sound_update(&surface, &materials, &field, 0, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(fabs(race.mixer.voices[20].left - .4) < 0.0000001);
	CHECK(fabs(race.mixer.voices[20].right - .08) < 0.0000001);
	CHECK(fabs(race.mixer.voices[15].left - .48) < 0.0000001);
	CHECK(fabs(race.mixer.voices[15].right - .096) < 0.0000001);
	CHECK(surface.mixer.voices[17].left >
		surface.mixer.voices[17].right * 4.99);
	CHECK(race.mixer.voices[20].pitch == horn_pitch &&
		race.mixer.voices[15].pitch == engine_pitch);

	return 0;
}

static int
deferred_positions(void)
{
	static const unsigned int ids[3] = {20, 15, MH_MIXER_VOICES + 17};
	struct mh_effects_mixer effects, before_effects;
	struct mh_race_sound before_race;
	struct mh_surface_sound before_surface;
	struct mh_sound_pool before_pool;
	struct mh_mixer *mixers[3];
	float output[32];
	unsigned int i, channel;

	CHECK(setup_fixture(1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 13, 3000, 0,
		0) == 0);
	mixers[0] = &race.mixer;
	mixers[1] = &surface.mixer;
	mixers[2] = &foreign;
	CHECK(mh_effects_init(48000, 1, &effects, NULL) == MH_ERROR_NONE);
	field.cars[0].vehicle.motion.position[2] = 200;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);
	before_pool = pool;
	before_race = race;
	before_surface = surface;

	/*
	 * Queue outside range, move before admission, and do not run producers.
	 * Horn/engine/skid requests and their cached parameters stay untouched.
	 */
	field.cars[0].vehicle.motion.position[2] = 0;
	field.cars[0].vehicle.motion.position[0] = 10;
	field.cars[0].vehicle.motion.position[1] = -.3199999928474426;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &before_pool, sizeof(pool)) == 0);
	CHECK(memcmp(race.mixer.voices, before_race.mixer.voices,
		sizeof(race.mixer.voices)) == 0);
	CHECK(memcmp(surface.mixer.voices, before_surface.mixer.voices,
		sizeof(surface.mixer.voices)) == 0);
	CHECK(surface.random == before_surface.random);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		channel = pool.records[ids[i]].channel;
		CHECK(channel != 0 &&
			effects.pcm.voices[channel - 1].right >
				effects.pcm.voices[channel - 1].left * 4.99);
	}

	CHECK(mh_effects_render(&effects, &pool, mixers, 3, output, 16, 0,
		NULL) == MH_ERROR_NONE);
	before_effects = effects;
	before_pool = pool;
	field.cars[0].camera.position[0] = 20;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&pool, &before_pool, sizeof(pool)) == 0);
	CHECK(memcmp(&effects, &before_effects, sizeof(effects)) == 0);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		channel = pool.records[ids[i]].channel - 1;
		CHECK(effects.pcm.voices[channel].left >
			effects.pcm.voices[channel].right * 4.99);
		CHECK(effects.cursors[channel] ==
				before_effects.cursors[channel] &&
			effects.fractions[channel] ==
				before_effects.fractions[channel]);
	}

	field.cars[0].vehicle.motion.position[2] = 200;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(!pool.records[ids[i]].active);

	field.cars[0].vehicle.motion.position[2] = 0;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_effects_update(&effects, &pool, mixers, 3, NULL) ==
		MH_ERROR_NONE);

	/* Movement cannot request a sound. */
	for (i = 0; i < 3; i++)
		CHECK(!pool.records[ids[i]].active);

	/* A late invalid car rolls back earlier geometry in both banks. */
	field.count = 2;
	field.cars[1] = field.cars[0];
	field.cars[0].vehicle.motion.position[0] = 30;
	field.cars[1].vehicle.motion.position[2] = 1e7;
	before_race = race;
	before_surface = surface;
	before_pool = pool;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &before_race, sizeof(race)) == 0 &&
		memcmp(&surface, &before_surface, sizeof(surface)) == 0 &&
		memcmp(&pool, &before_pool, sizeof(pool)) == 0);

	return 0;
}

static int
larger_roster(void)
{
	unsigned int i, horn, engine, skid;

	CHECK(setup_fixture(13, 12, 1U << 12, 8191, 0, 8191, 8191, 0, 0, 1, 0,
		0, 0, 13, 9000, 0, .5) == 0);
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);

	for (i = 8; i < 13; i++) {
		horn = mh_race_horn_voice(i);
		engine = mh_race_engine_voice(i);
		skid = mh_surface_skid_voice(i);
		CHECK(horn < MH_MIXER_VOICES && engine < MH_MIXER_VOICES);
		CHECK(horn != engine && skid < MH_MIXER_VOICES);
		CHECK(race.mixer.voices[horn].wave == &bank.horns[i].wave);
		CHECK(race.mixer.spatial[horn].bound == (i != 12));
	}

	CHECK(!race.mixer.voices[MH_COUNTDOWN_GO].active);
	CHECK(!race.mixer.voices[MH_RACE_VOICE_FINISH_COPY].active);
	field.cars[12].vehicle.motion.position[0] = 17;
	CHECK(mh_vehicle_sound_positions(&race, &surface, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.mixer.spatial[mh_race_horn_voice(12)].position[0] == 17);
	CHECK(surface.mixer.spatial[mh_surface_skid_voice(12)].position[0] ==
		17);
	field.active_mask = 1;
	field.viewed_entry = 0;
	field.listener_mask = 1;
	CHECK(mh_vehicle_sound_update(&race, &bank, &surface, &materials,
		&field, 0, 0, NULL) == MH_ERROR_NONE);

	for (i = 8; i < 13; i++) {
		CHECK(!race.mixer.voices[mh_race_horn_voice(i)].active);
		CHECK(!race.mixer.voices[mh_race_engine_voice(i)].active);
		CHECK(!surface.mixer.voices[mh_surface_skid_voice(i)].active);
	}

	return 0;
}

int
main(void)
{
	if (larger_roster())
		return 1;

	return atomicity() || completion() || multiple_owners() ||
		positional() || listener_pose() || deferred_positions();
}
