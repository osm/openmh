#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/particles.h"
#include "engine/simulation/tyre_effects.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_tyre_effects(void)
{
	struct mh_materials table;
	struct mh_tyre_effects state, saved;
	struct mh_tyre_effect_input input;

	memset(&table, 0, sizeof(table));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	table.count = 2;
	table.entries[0].contact_anim = 1;
	table.entries[1].contact_anim = 3;
	input.supported[0] = input.supported[1] = 1;
	input.material[1] = 1;
	input.sliding = 1;
	input.slide = .2;
	input.forward = 3;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.id[0] == 0);
	input.forward = 4;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.id[0] == 1 && state.id[1] == 3);
	CHECK(state.gain[0] == (double)(float).1);
	input.wheelspin = 1;
	input.rpm = 4000;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gain[0] == (double)(float).7);
	input.heavy_brake = 1;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gain[0] == (double)(float).4);
	input.supported[0] = input.supported[1] = 0;
	saved = state;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	mh_tyre_effects_begin_frame(&state);
	CHECK(state.id[0] == 0 && state.id[1] == 0 &&
		state.gain[0] == saved.gain[0]);
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.id[0] == 0);
	input.supported[0] = 1;
	input.material[0] = 26;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.id[0] == 1);
	mh_tyre_effects_begin_frame(&state);
	table.entries[0].contact_anim = 9;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) ==
			MH_ERROR_NONE &&
		state.id[0] == 0);
	table.entries[0].contact_anim = 1;
	input.supported[1] = 1;
	input.material[1] = 2;
	saved = state;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	input.material[1] = 1;
	input.forward = 1e5;
	CHECK(mh_tyre_effects_update(&table, &input, &state, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_tyre_effects_update(NULL, &input, &state, &state, NULL) !=
		MH_ERROR_NONE);
	mh_tyre_effects_begin_frame(NULL);
}

int
main(void)
{
	struct mh_particle_system system, saved_system;
	struct mh_particles state, saved;
	struct mh_particle_input input;
	struct mh_particle *smoke, *grass, *spark;
	unsigned int i;
	float smoke_y, grass_y, width;

	check_tyre_effects();
	memset(&input, 0, sizeof(input));
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.tyre_id[0] = 1;
	input.tyre_id[1] = 2;
	input.spark_id[0] = 1;
	input.spark_gain[0] = .5f;
	CHECK(mh_particle_system_init(&system, 1, 1, NULL) == MH_ERROR_NONE);
	mh_particles_reset(&state);
	CHECK(mh_particles_emit(&input, &system, &state, NULL) ==
		MH_ERROR_NONE);
	smoke = &state.pools[0].particles[0];
	grass = &state.pools[1].particles[0];
	spark = &state.pools[2].particles[0];
	CHECK(smoke->animation == MH_PARTICLE_SMOKE &&
		grass->animation == MH_PARTICLE_GRASS);
	CHECK(smoke->frame == 1 && grass->frame == 1 && spark->frame == 1 &&
		spark->phase == 1);
	CHECK(spark->style == 1 && spark->width == .375f &&
		spark->color[0] == 6);
	smoke_y = smoke->position[1];
	grass_y = grass->position[1];
	width = smoke->width;
	CHECK(mh_particles_advance(&system, .06f, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(smoke->position[1] > smoke_y && smoke->width > width &&
		grass->position[1] == grass_y);
	CHECK(spark->active && spark->phase == 2);
	CHECK(mh_particles_advance(&system, 0, &state, NULL) == MH_ERROR_NONE &&
		!spark->active && spark->phase == 0);
	CHECK(mh_particles_advance(&system, 2, &state, NULL) == MH_ERROR_NONE &&
		!smoke->active && !grass->active);
	smoke_y = smoke->position[1];
	width = smoke->width;
	state.pools[0].enabled = 0;
	CHECK(mh_particles_advance(&system, .1f, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(smoke->position[1] > smoke_y && smoke->width > width &&
		!state.pools[0].enabled);
	CHECK(state.pools[0].count == 1 && state.pools[1].count == 1 &&
		state.pools[2].count == 1);

	/*
	 * Long quiet sessions must never overflow expired, invisible records.
	 */
	for (i = 0; i < 1000; i++)
		CHECK(mh_particles_advance(&system, 10, &state, NULL) ==
			MH_ERROR_NONE);

	CHECK(smoke->width == 1e30f && smoke->height == 1e30f &&
		!smoke->active);
	input.tyre_id[0] = 3;
	input.tyre_id[1] = 0;
	input.spark_gain[0] = -1;

	for (i = 0; i < 40; i++)
		CHECK(mh_particles_emit(&input, &system, &state, NULL) ==
			MH_ERROR_NONE);

	CHECK(state.pools[0].count == 16 && state.pools[0].first == 9 &&
		state.pools[0].enabled);
	CHECK(state.pools[1].count == 1 && state.pools[2].count == 1);
	CHECK(state.pools[0].particles[8].animation == MH_PARTICLE_SMOKE &&
		state.pools[0].particles[8].color[0] == .7f);
	input.force_smoke = 1;
	CHECK(mh_particles_emit(&input, &system, &state, NULL) ==
			MH_ERROR_NONE &&
		state.pools[1].count == 2);
	CHECK(state.pools[1].particles[1].animation == MH_PARTICLE_SMOKE);
	saved = state;
	saved_system = system;
	input.spark_corner[1] = 7;
	CHECK(mh_particles_emit(&input, &system, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&system, &saved_system, sizeof(system)) == 0);
	input.spark_corner[1] = 0;
	CHECK(mh_particles_advance(&system, -1, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	state.pools[2].particles[0].animation = 5;
	saved = state;
	CHECK(mh_particles_advance(&system, .1f, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_particles_emit(&input, &system, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(memcmp(&system, &saved_system, sizeof(system)) == 0);
	CHECK(mh_particle_system_init(&system, 2, 1, NULL) != MH_ERROR_NONE &&
		memcmp(&system, &saved_system, sizeof(system)) == 0);
	CHECK(mh_particles_emit(NULL, &system, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_particles_emit(&input, NULL, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_particles_advance(&system, .1f, NULL, NULL) != MH_ERROR_NONE);
	mh_particles_reset(NULL);
	CHECK(mh_particle_system_init(&system, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(system.animations[0].frames == 17 &&
		system.animations[3].frames == 20);
	CHECK(system.animations[2].flags == 4 &&
		system.animations[3].frame_seconds == (float)(1.0 / 60));

	return failures != 0;
}
