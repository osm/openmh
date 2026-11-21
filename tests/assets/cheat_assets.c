#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/drive_particles.h"

static struct mh_race_context race_context;
static struct mh_field field, initial, normal, saved;

static int
capture_muted(const char *root, const char *path)
{
	struct mh_audio_options audio;

	memset(&audio, 0, sizeof(audio));

	return mh_race_run(
		&race_context, &field, path, 1, 0, root, &audio, NULL, NULL);
}

static enum mh_error_code
check_visual_mode(unsigned int flags, struct mh_error *error_out)
{
	struct mh_engine_input controls;
	enum mh_error_code status;
	unsigned int i, tick, steps;

	normal = field;
	status = mh_field_set_cheats(&field, flags, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	field.network = 1;
	saved = field;

	if (mh_field_set_cheats(&field, flags, NULL) == MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"visual mode accepted network field or changed rejected setup");

	field.network = 0;

	for (tick = 0; tick < 180 && status == MH_ERROR_NONE; tick++) {
		memset(&controls, 0, sizeof(controls));
		controls.throttle = .8;
		controls.steering = tick % 60 < 30 ? .1 : -.1;

		if (tick == 60 || tick == 120) {
			status = mh_field_reset(&field, tick == 120, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_field_reset(
					&normal, tick == 120, error_out);
		}

		if (status == MH_ERROR_NONE && tick % 30 == 0)
			status = mh_field_cycle_view(&field, error_out);

		if (status == MH_ERROR_NONE && tick % 30 == 0)
			status = mh_field_cycle_view(&normal, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&field, &controls, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&normal, &controls, error_out);

		if (status != MH_ERROR_NONE)
			break;

		saved = field;

		for (i = 0; i < field.count; i++) {
			if (field.cars[i].cheats != flags)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"visual mode reset lost entrant flag");

			saved.cars[i].cheats = 0;
		}

		if (memcmp(&saved, &normal, sizeof(field)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"visual mode changed simulation/camera/clocks");
	}

	if (status != MH_ERROR_NONE)
		return status;

	saved = field;
	status = mh_field_advance(&field, &controls, .25, 1, &steps, error_out);

	if (status == MH_ERROR_NONE &&
		(steps != 0 || memcmp(&field, &saved, sizeof(field)) != 0))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"paused visual mode simulation advanced");

	if (status == MH_ERROR_NONE)
		printf("%s %u entrants 180 ticks: unchanged physics/cameras/clocks, view handoffs, pause and resets passed\n",
			flags == MH_CHEAT_TRON ? "TRON" : "RETRO", field.count);

	return status;
}

static enum mh_error_code
check_thunder(const char *root, const char *prefix, struct mh_error *error_out)
{
	static const unsigned long seeds[4] = {
		790079107UL, 444376707UL, 293774979UL, 4243039875UL};

	struct mh_engine_input controls;
	struct mh_particle_system system;
	struct mh_particles particles;
	struct mh_drive emitter;
	struct mh_vertex *world, *body, expected;
	const struct mh_weather_style *style;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	unsigned int i, j, phase, tick, steps;
	unsigned long seed;
	size_t n, changed;
	double value;
	char path[1024];
	enum mh_error_code status;

	initial = field;
	status = mh_field_set_cheats(
		&field, mh_cheat_flags("Lemmy", "Ace"), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	field.network = 1;
	saved = field;

	if (mh_field_set_cheats(&field, MH_CHEAT_THUNDER, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"thunder accepted network field or changed rejection");

	field.network = 0;
	world = (struct mh_vertex *)malloc(
		race_context.scene.assets.meshes[0].vertex_count *
		sizeof(*world));
	body = (struct mh_vertex *)malloc(
		race_context.scene.assets.meshes[1].vertex_count *
		sizeof(*body));

	if (world == NULL || body == NULL) {
		free(world);
		free(body);
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"out of memory checking storm lighting");
	}

	memcpy(world, race_context.scene.assets.meshes[0].vertices,
		race_context.scene.assets.meshes[0].vertex_count *
			sizeof(*world));
	mh_field_matrices(&field, 1.5, models, matrix, view);
	changed = 0;

	for (phase = 0; phase < 4 && status == MH_ERROR_NONE; phase++) {
		race_context.scene.visuals.particle_system.random =
			seeds[phase];
		saved = field;
		status = mh_race_scene_weather(&race_context.scene, &field,
			race_context.settings, error_out);
		style = race_context.scene.visuals.weather;

		if (status != MH_ERROR_NONE)
			break;

		if (memcmp(&field, &saved, sizeof(field)) != 0 ||
			style == NULL ||
			race_context.scene.visuals.particle_system.random !=
				((seeds[phase] * 1103515245UL + 12345UL) &
					MH_U32_MAX) ||
			(style->brightness[0] == 3) !=
				(phase == 1 || phase == 2)) {
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"storm frame changed simulation or selected wrong random style");
			break;
		}

		for (n = 0;
			n < race_context.scene.assets.meshes[0].vertex_count &&
			status == MH_ERROR_NONE;
			n++) {
			expected = world[n];

			if (n < race_context.scene.assets.meshes[0]
					.world_vertex_count)
				for (j = 0; j < 3; j++) {
					value = race_context.scene.assets
							.meshes[0]
							.world_colors[n * 3 +
								j] *
						style->brightness[j];
					expected.color[j] =
						(float)((value > 255 ? 255
								: value) /
							255);
				}

			if (memcmp(&expected,
				&race_context.scene.assets.meshes[0]
					.vertices[n],
				sizeof(expected)) != 0)
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"storm world colors or unrelated geometry differ");
		}

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_shade_car(&race_context.scene,
				&field, 1, models, view, 0, 1, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (phase == 0)
			memcpy(body,
				race_context.scene.visuals.shaded_vertices[1],
				race_context.scene.assets.meshes[1]
						.vertex_count *
					sizeof(*body));

		if (phase == 1)
			for (n = 0; n < race_context.scene.assets.meshes[1]
						.vertex_count;
				n++)
				if (memcmp(body[n].color,
					race_context.scene.visuals
						.shaded_vertices[1][n]
						.color,
					3 * sizeof(float)) != 0)
					changed++;

		if (phase == 3 &&
			memcmp(body,
				race_context.scene.visuals.shaded_vertices[1],
				race_context.scene.assets.meshes[1]
						.vertex_count *
					sizeof(*body)) != 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"storm car lighting failed to restore dark frame");
	}

	if (status == MH_ERROR_NONE && changed == 0)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"storm flash did not reach car lighting");

	if (status == MH_ERROR_NONE)
		status = mh_field_set_cheats(&field, 0, error_out);

	seed = race_context.scene.visuals.particle_system.random;

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_weather(&race_context.scene, &field,
			race_context.settings, error_out);

	if (status == MH_ERROR_NONE &&
		(race_context.scene.visuals.weather != NULL ||
			seed !=
				race_context.scene.visuals.particle_system
					.random ||
			memcmp(world,
				race_context.scene.assets.meshes[0].vertices,
				race_context.scene.assets.meshes[0]
						.vertex_count *
					sizeof(*world)) != 0))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"ordinary style did not restore world or consumed randomness");

	free(world);
	free(body);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_field_set_cheats(&field, MH_CHEAT_THUNDER, error_out);

	for (i = 0; i < field.count && status == MH_ERROR_NONE; i++) {
		emitter = field.cars[i];
		emitter.vehicle.grip.tyre_effects.id[0] = 2;
		emitter.vehicle.grip.tyre_effects.id[1] = 0;
		mh_particles_reset(&particles);
		status = mh_particle_system_init(&system, 1, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_particles_emit(
				&emitter, &system, &particles, error_out);

		if (status == MH_ERROR_NONE &&
			(particles.pools[0].count != 1 ||
				particles.pools[1].count != 0 ||
				particles.pools[0].particles[0].animation !=
					MH_PARTICLE_GRASS))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"thunder incorrectly forced underwater smoke");
	}

	normal = initial;

	for (tick = 0; tick < 180 && status == MH_ERROR_NONE; tick++) {
		memset(&controls, 0, sizeof(controls));
		controls.throttle = .8;
		controls.steering = tick % 60 < 30 ? .1 : -.1;

		if (tick == 60 || tick == 120) {
			status = mh_field_reset(&field, tick == 120, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_field_reset(
					&normal, tick == 120, error_out);
		}

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&field, &controls, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&normal, &controls, error_out);

		if (status != MH_ERROR_NONE)
			break;

		saved = field;

		for (i = 0; i < field.count; i++) {
			if (field.cars[i].cheats != MH_CHEAT_THUNDER)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"thunder reset lost entrant flag");

			saved.cars[i].cheats = 0;
		}

		if (memcmp(&saved, &normal, sizeof(field)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"thunder changed simulation/camera/clocks");

		status = mh_race_scene_update_particles(
			&race_context.scene, &field, error_out);
		seed = race_context.scene.visuals.particle_system.random;

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_weather(&race_context.scene,
				&field, race_context.settings, error_out);

		if (status == MH_ERROR_NONE &&
			race_context.scene.visuals.particle_system.random !=
				((seed * 1103515245UL + 12345UL) & MH_U32_MAX))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"thunder did not consume particle stream exactly once");
	}

	if (status != MH_ERROR_NONE)
		return status;

	saved = field;
	seed = race_context.scene.visuals.particle_system.random;
	status = mh_field_advance(&field, &controls, .25, 1, &steps, error_out);

	if (status == MH_ERROR_NONE &&
		(steps != 0 || memcmp(&field, &saved, sizeof(field)) != 0 ||
			seed !=
				race_context.scene.visuals.particle_system
					.random))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"paused thunder simulation advanced");

	if (prefix != NULL && status == MH_ERROR_NONE) {
		if (strlen(prefix) > 900)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"storm capture prefix too long");

		status = mh_hud_assets_load(root, &race_context.hud, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_results_assets_load(root, field.count,
				"Lemmy", &race_context.hud.atlas,
				&race_context.results, error_out);

		for (phase = 0; phase < 2 && status == MH_ERROR_NONE; phase++) {
			race_context.scene.visuals.particle_system.random =
				seeds[phase];
			sprintf(path, "%s-%s.bmp", prefix,
				phase ? "flash" : "dark");

			if (!capture_muted(root, path))
				status =
					mh_error_set(error_out, MH_ERROR_FAILED,
						"storm capture failed");

			if (status == MH_ERROR_NONE &&
				race_context.scene.visuals.particle_system
						.random !=
					((seeds[phase] * 1103515245UL +
						12345UL) &
						MH_U32_MAX))
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"headless depth-history draw consumed a second flash decision");
		}
	}

	if (status == MH_ERROR_NONE)
		printf("thunder %u entrants 180 ticks: shared RNG, lighting transitions, unchanged simulation, smoke, pause and resets passed\n",
			field.count);

	return status;
}

static enum mh_error_code
check_underwater(struct mh_error *error_out)
{
	struct mh_engine_input controls;
	struct mh_vehicle_input input;
	struct mh_vehicle_result expected;
	struct mh_vehicle_tuning tuning;
	struct mh_drive drive;
	struct mh_particle_system system;
	struct mh_particles particles;
	struct mh_car original;
	struct mh_track track_original;
	struct mh_vertex *vertices;
	enum mh_error_code status;
	unsigned int i, j, tick, gate, props, moved, steps;
	size_t n;
	double interval, low[3], elapsed;

	initial = field;
	original = race_context.scene.assets.car;
	track_original = race_context.scene.assets.track;
	status = mh_field_set_cheats(
		&field, mh_cheat_flags("Ramlosa", "H2O"), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		if (field.cars[i].cheats != MH_CHEAT_UNDERWATER)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"water mode missed entrant");

	field.network = 1;
	saved = field;

	if (mh_field_set_cheats(&field, MH_CHEAT_UNDERWATER, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"water accepted network field or modified rejection");

	field.network = 0;

	for (gate = 0; gate < 3; gate++) {
		drive = field.cars[0];
		drive.ai_path = NULL;
		drive.countdown = gate != 2;
		drive.start_ticks = gate == 0 ? 1439 : 1440;
		interval = gate == 0 ? MH_DRIVE_DT : MH_DRIVE_DT * .5;
		memset(&controls, 0, sizeof(controls));
		controls.throttle = .7;
		controls.steering = .2;
		memset(&input, 0, sizeof(input));
		input.controls = controls;
		input.forced_detach = input.forced_stop = gate == 0;
		tuning.turn_force = drive.car->turn_force;
		tuning.brake_force = drive.car->brake_force;
		tuning.grip_level = drive.car->grip_level;
		tuning.suspension = &drive.suspension;
		status = mh_vehicle_step_tuned(drive.car, drive.col,
			drive.materials, &drive.config, &tuning, &drive.vehicle,
			&input, interval, &expected, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_tick(&drive, &controls, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (memcmp(&drive.vehicle, &expected.state,
			sizeof(expected.state)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"full vehicle update did not use selected physics interval");

		if (drive.seconds != MH_DRIVE_DT ||
			drive.race_ticks != (gate == 0 ? 0UL : 1UL))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"water physics changed race clock rate");
	}

	/*
	 * Gravity gives an independent interval observation for every entrant
	 * and each static loose prop, without road or pair impulses.
	 */
	for (i = 0; i < field.count; i++)
		field.cars[i].vehicle.motion.position[1] = 500;

	props = 0;

	for (i = 0; i < field.object_count; i++)
		if (field.objects[i].source.scene->key_count <= 1) {
			field.objects[i].state.flags = 1;
			field.objects[i].state.motion.position[1] = 500;
			props++;
		}

	normal = field;
	status = mh_field_set_cheats(&normal, 0, error_out);
	memset(&controls, 0, sizeof(controls));

	if (status == MH_ERROR_NONE)
		status = mh_field_tick(&normal, &controls, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_tick(&field, &controls, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++) {
		if (!(field.cars[i].vehicle.motion.position[1] >
			normal.cars[i].vehicle.motion.position[1]))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"water did not slow an airborne entrant");

		if (field.cars[i].seconds != normal.cars[i].seconds ||
			field.cars[i].race_ticks != normal.cars[i].race_ticks)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"water changed entrant clocks");
	}

	for (i = 0; i < field.object_count; i++)
		if (field.objects[i].source.scene->key_count <= 1)
			if (fabs(field.objects[i].state.motion.velocity[1] -
				.5 *
					normal.objects[i]
						.state.motion.velocity[1] /
					pow((double)(float).9,
						MH_DRIVE_DT * .5)) > 1e-10)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"water prop did not receive half interval");

	if (field.scene_seconds != normal.scene_seconds)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"water changed animation clock");

	/*
	 * Force a material-independent emitter, including an unsupported tyre.
	 */
	field = initial;
	status = mh_field_set_cheats(&field, MH_CHEAT_UNDERWATER, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++) {
		field.cars[i].vehicle.grip.tyre_effects.id[0] = 0;
		field.cars[i].vehicle.grip.tyre_effects.id[1] = 3;
		mh_particles_reset(&particles);
		status = mh_particle_system_init(&system, 1, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_particles_emit(
				&field.cars[i], &system, &particles, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < 2; j++)
			if (particles.pools[j].count != 1 ||
				particles.pools[j].particles[0].animation !=
					MH_PARTICLE_SMOKE)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"water failed to force smoke");

		for (j = 0; j < 4; j++) {
			race_context.scene.visuals.skids[i].wheels[j].active =
				race_context.scene.visuals.skids[i]
					.wheels[j]
					.committed =
					race_context.scene.visuals.skids[i]
						.wheels[j]
						.partial = 1;
			race_context.scene.visuals.skids[i].wheels[j].count = 1;
			race_context.scene.visuals.skids[i]
				.wheels[j]
				.segments[0]
				.color[1] = .25f;
		}
	}

	status = mh_race_scene_skids(&race_context.scene, &field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		for (j = 0; j < 4; j++)
			if (race_context.scene.visuals.skids[i]
					.wheels[j]
					.active ||
				race_context.scene.visuals.skids[i]
					.wheels[j]
					.committed ||
				race_context.scene.visuals.skids[i]
					.wheels[j]
					.partial ||
				race_context.scene.visuals.skids[i]
						.wheels[j]
						.count != 1 ||
				race_context.scene.visuals.skids[i]
						.wheels[j]
						.segments[0]
						.color[1] != .25f)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"water skid suppression changed completed history");

	/*
	 * A saturated brightness round trip must recover original bytes and
	 * leave every appended scenery vertex untouched.
	 */
	vertices = (struct mh_vertex *)malloc(
		race_context.scene.assets.meshes[0].vertex_count *
		sizeof(*vertices));

	if (vertices == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"out of memory checking world colors");

	memcpy(vertices, race_context.scene.assets.meshes[0].vertices,
		race_context.scene.assets.meshes[0].vertex_count *
			sizeof(*vertices));
	low[0] = low[1] = low[2] = 100;
	status = mh_mesh_world_brightness(
		&race_context.scene.assets.meshes[0], low, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mesh_world_brightness(
			&race_context.scene.assets.meshes[0],
			mh_water_style()->brightness, error_out);

	for (n = race_context.scene.assets.meshes[0].world_vertex_count;
		status == MH_ERROR_NONE &&
		n < race_context.scene.assets.meshes[0].vertex_count;
		n++)
		if (memcmp(&vertices[n],
			&race_context.scene.assets.meshes[0].vertices[n],
			sizeof(*vertices)) != 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"world override tinted scenery");

	if (status == MH_ERROR_NONE)
		status = mh_mesh_world_brightness(
			&race_context.scene.assets.meshes[0],
			race_context.scene.visuals.brightness.world, error_out);

	if (status == MH_ERROR_NONE &&
		memcmp(vertices, race_context.scene.assets.meshes[0].vertices,
			race_context.scene.assets.meshes[0].vertex_count *
				sizeof(*vertices)) != 0)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"world color restoration changed vertices");

	free(vertices);

	if (status != MH_ERROR_NONE)
		return status;

	field = initial;
	status = mh_field_set_cheats(&field, MH_CHEAT_UNDERWATER, error_out);
	moved = 0;

	for (tick = 0; tick < 1800 && status == MH_ERROR_NONE; tick++) {
		memset(&controls, 0, sizeof(controls));
		controls.throttle = .8;
		controls.steering = tick % 240 < 120 ? .12 : -.12;

		if (tick % 300 > 250) {
			controls.throttle = 0;
			controls.brake = 1;
		}

		if (tick == 600 || tick == 1200)
			status =
				mh_field_reset(&field, tick == 1200, error_out);

		elapsed = field.scene_seconds;

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&field, &controls, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (tick == 300) {
			saved = field;

			if (mh_field_set_cheats(&field, 0, NULL) ==
					MH_ERROR_NONE ||
				memcmp(&field, &saved, sizeof(field)) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"water allowed mid-race mode change");

			status = mh_field_advance(
				&field, &controls, .25, 1, &steps, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (steps != 0 ||
				memcmp(&field, &saved, sizeof(field)) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"paused water field advanced");
		}

		if (fabs(field.scene_seconds - elapsed - MH_DRIVE_DT) > 1e-10)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"water scene clock changed rate");

		for (i = 0; i < field.count; i++) {
			if (field.cars[i].cheats != MH_CHEAT_UNDERWATER)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"water reset lost an entrant flag");

			if (memcmp(field.cars[i].vehicle.motion.position,
				initial.cars[i].vehicle.motion.position,
				3 * sizeof(double)) != 0)
				moved |= 1U << i;
		}
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (moved != (1U << field.count) - 1)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"water fixture failed to move all cars");

	if (memcmp(&original, &race_context.scene.assets.car,
		sizeof(original)) ||
		memcmp(&track_original, &race_context.scene.assets.track,
			sizeof(track_original)))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"water modified parsed CAR/TRK");

	printf("water %u entrants %u props 1800 ticks: start gate, clocks, physics, smoke, skids, colors and resets passed\n",
		field.count, props);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_suspension(struct mh_error *error_out)
{
	static const unsigned int modes[2] = {MH_CHEAT_DEMON, MH_CHEAT_G_RIDE};
	struct mh_suspension_config expected[MH_TRAFFIC_MAX_CARS];
	struct mh_engine_input controls;
	struct mh_car broken;
	struct mh_car originals[MH_TRAFFIC_MAX_CARS];
	const struct mh_car *opponents[MH_TRAFFIC_MAX_CARS - 1];
	const struct mh_drive *drive;
	const struct mh_motion_state *motion;
	enum mh_error_code status;
	unsigned int mode, i, j, k, axis, tick, supported, changed;
	float models[MH_DRIVE_MESHES * 16], matrix[16], view[16];
	double local[3], position[3], velocity[3], coordinate;

	initial = field;

	for (i = 0; i < field.count; i++)
		originals[i] = *field.cars[i].car;

	for (i = 1; i < field.count; i++)
		opponents[i - 1] = field.cars[i].car;

	/*
	 * A last-entrant validation failure must not publish earlier settings.
	 */
	broken = *field.cars[field.count - 1].car;
	broken.spring_length = 4000;
	field.cars[field.count - 1].car = &broken;
	saved = field;

	if (mh_field_set_cheats(&field, MH_CHEAT_DEMON, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"invalid last entrant changed earlier springs");

	field = initial;
	field.network = 1;
	saved = field;

	if (mh_field_set_cheats(&field, MH_CHEAT_G_RIDE, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"network suspension mode accepted");

	for (mode = 0; mode < 2; mode++) {
		field = initial;
		status = mh_field_set_cheats(&field, modes[mode], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < field.count; i++) {
			status = mh_suspension_configure(initial.cars[i].car,
				modes[mode], &expected[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (memcmp(&field.cars[i].config,
				&initial.cars[i].config,
				sizeof(field.cars[i].config)) != 0 ||
				field.cars[i].car != initial.cars[i].car)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"suspension secret altered engine or asset binding");
		}

		status = mh_field_start(&field.cars[0], opponents,
			field.count - 1, field.path, 2, &normal, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < normal.count; i++)
			if (normal.cars[i].cheats != modes[mode] ||
				memcmp(&normal.cars[i].suspension, &expected[i],
					sizeof(expected[i])) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"new opponent failed to inherit hidden suspension");

		normal = initial;
		supported = changed = 0;

		for (tick = 0; tick < 1200; tick++) {
			if (tick == 600 || tick == 900) {
				status = mh_field_reset(
					&field, tick == 900, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

			memset(&controls, 0, sizeof(controls));
			controls.throttle = .75;
			controls.steering = tick % 240 < 120 ? .12 : -.12;

			if (tick % 300 > 250) {
				controls.throttle = 0;
				controls.brake = 1;
			}

			status = mh_field_tick(&field, &controls, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (tick < 120) {
				status = mh_field_tick(
					&normal, &controls, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				for (i = 0; i < field.count; i++)
					if (memcmp(&normal.cars[i]
							.vehicle.motion,
						&field.cars[i]
							.vehicle.motion,
						sizeof(field.cars[i]
								.vehicle
								.motion)) !=
						0)
						changed |= 1U << i;
			}

			if (tick == 0) {
				saved = field;

				if (mh_field_set_cheats(&field, 0, NULL) ==
						MH_ERROR_NONE ||
					memcmp(&field, &saved, sizeof(field)) !=
						0)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"active suspension reconfiguration changed field");
			}

			for (i = 0; i < field.count; i++) {
				drive = &field.cars[i];
				motion = &drive->vehicle.motion;

				if (drive->cheats != modes[mode] ||
					memcmp(&drive->suspension, &expected[i],
						sizeof(expected[i])) != 0)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"tick or reset lost suspension mode");

				for (j = 0; j < 4; j++)
					supported +=
						drive->vehicle.grip.wheels[j]
							.supported != 0;

				if (tick % 120 != 0)
					continue;

				status = mh_motion_display(
					motion, position, velocity, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				mh_drive_matrices(
					drive, 1.5, models, matrix, view);

				for (j = 0; j < 4; j++) {
					local[0] =
						drive->car
							->wheel_position[j][0] *
						1.25;
					local[1] =
						drive->car
							->wheel_position[j][1] -
						expected[i].length *
							motion->extension[j];
					local[2] =
						drive->car
							->wheel_position[j][2];

					for (axis = 0; axis < 3; axis++) {
						coordinate = position[axis];

						for (k = 0; k < 3; k++)
							coordinate +=
								motion->rotation[k *
										3 +
									axis] *
								local[k];

						if (models[(j + 2) * 16 + 12 +
							axis] !=
							(float)coordinate)
							return mh_error_set(
								error_out,
								MH_ERROR_FAILED,
								"wheel presentation lost runtime spring length");
					}
				}
			}
		}

		if (supported == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"suspension drive fixture never touched road");

		if (changed != (1U << field.count) - 1)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"suspension mode left an entrant's physics unchanged");

		for (i = 0; i < field.count; i++)
			if (memcmp(field.cars[i].car, &originals[i],
				sizeof(originals[i])) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"suspension mode modified immutable CAR data");

		if (memcmp(&field.cars[0].vehicle, &initial.cars[0].vehicle,
			sizeof(field.cars[0].vehicle)) == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"suspension fixture did not move");

		printf("springs %u %u 1200 %u\n", modes[mode], field.count,
			supported);
	}

	field = initial;
	status = mh_field_set_cheats(&field, MH_CHEAT_DEMON, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_cheats(&field, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		if (memcmp(&field.cars[i].suspension,
			&initial.cars[i].suspension,
			sizeof(expected[i])) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"ordinary setup failed to restore CAR springs");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_moon(struct mh_error *error_out)
{
	struct mh_engine_input controls;
	unsigned int i, props;
	enum mh_error_code status;

	initial = field;
	status = mh_field_set_cheats(
		&field, mh_cheat_flags("Buzz Aldrin", "NASA"), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++) {
		if (field.cars[i].cheats != MH_CHEAT_MOON)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"moon flag missed an entrant");

		field.cars[i].vehicle.motion.position[1] = 500;
	}

	props = 0;

	for (i = 0; i < field.object_count; i++)
		if (field.objects[i].source.scene->key_count <= 1) {
			field.objects[i].state.flags = 1;
			field.objects[i].state.motion.position[1] = 500;
			props++;
		}

	normal = field;
	status = mh_field_set_cheats(&normal, 0, error_out);
	memset(&controls, 0, sizeof(controls));

	if (status == MH_ERROR_NONE)
		status = mh_field_tick(&normal, &controls, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_tick(&field, &controls, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		if (!(field.cars[i].vehicle.motion.position[1] >
				normal.cars[i].vehicle.motion.position[1] &&
			field.cars[i].vehicle.motion.position[1] < 500))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"entrant gravity did not change");

	for (i = 0; i < field.object_count; i++)
		if (field.objects[i].source.scene->key_count <= 1) {
			if (!(field.objects[i].state.motion.position[1] >
					normal.objects[i]
						.state.motion.position[1] &&
				field.objects[i].state.motion.position[1] <
					500))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"prop gravity did not change");

			if (fabs(field.objects[i].state.motion.velocity[1] -
				.5 *
					normal.objects[i]
						.state.motion.velocity[1]) >
				1e-10)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"prop did not receive shared half gravity");
		}

	saved = field;

	if (mh_field_set_cheats(&field, 0, NULL) == MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"active cheat setup was not rejected atomically");

	status = mh_field_reset(&field, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_reset(&field, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		if (field.cars[i].cheats != MH_CHEAT_MOON)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"reset lost moon mode");

	field = initial;
	saved = field;

	if (mh_field_set_cheats(&field, MH_CHEAT_RETRO | MH_CHEAT_TRON, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&field, &saved, sizeof(field)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"combined cheats were not rejected atomically");

	printf("moon %u cars %u props\n", field.count, props);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_special_mode(const char *mode, struct mh_error *error_out)
{
	if (strcmp(mode, "--water") == 0)
		return check_underwater(error_out);

	if (strcmp(mode, "--tron") == 0)
		return check_visual_mode(
			mh_cheat_flags("tribute to tron", "any team"),
			error_out);

	if (strcmp(mode, "--retro") == 0)
		return check_visual_mode(
			mh_cheat_flags("r3tr0", "a500"), error_out);

	return check_suspension(error_out);
}

int
main(int argc, char *argv[])
{
	static const double elapsed[5] = {
		1.0 / 30, 1.0 / 60, 1.0 / 120, 1.0 / 144, .001};

	struct mh_drive drive;
	struct mh_camera_pose prior;
	struct mh_engine_input controls;
	const struct mh_car *opponents[7];
	struct mh_error error;
	enum mh_error_code status;
	const char *name;
	const char *capture;
	unsigned int car, track, i, j, viewed, steps, request, cheat, level;
	int supercars;
	unsigned long listener;
	double position[3], velocity[3];
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];

	if (argc < 4 || argc > 6)
		return 2;

	supercars = argc > 4 && strcmp(argv[4], "--supercars") == 0;

	if (argc == 6 && !supercars && strcmp(argv[4], "--thunder") != 0)
		return 2;

	capture = supercars ? (argc == 6 ? argv[5] : NULL)
			: (argc == 5 ? argv[4] : NULL);

	name = supercars ? "Supercars" : "Ignition";
	cheat = mh_cheat_flags(name, supercars ? "Grem" : "UDS");
	level = supercars ? 3 : 2;
	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);
	status = mh_race_context_load(
		argv[1], car, track, 1, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai(
			argv[1], &race_context.scene, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_add_opponents(
			argv[1], car, track, 7, &race_context.scene, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			&error);

	for (i = 0; i < 7; i++)
		opponents[i] = &race_context.scene.assets.opponents[i];

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, opponents, 7,
			&race_context.scene.assets.ai_path, 2, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, field.count,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&race_context.scene, &field, &error);

	if (argc >= 5 && strcmp(argv[4], "--thunder") == 0) {
		if (status == MH_ERROR_NONE)
			status = check_thunder(
				argv[1], argc == 6 ? argv[5] : NULL, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "%s\n", error.message);

		mh_race_context_free(&race_context);

		return status != MH_ERROR_NONE || ferror(stdout);
	}

	if (argc == 5 &&
		(strcmp(argv[4], "--suspension") == 0 ||
			strcmp(argv[4], "--water") == 0 ||
			strcmp(argv[4], "--tron") == 0 ||
			strcmp(argv[4], "--retro") == 0)) {
		if (status == MH_ERROR_NONE)
			status = check_special_mode(argv[4], &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "%s\n", error.message);

		mh_race_context_free(&race_context);

		return status != MH_ERROR_NONE || ferror(stdout);
	}

	if (status == MH_ERROR_NONE && !supercars)
		status = check_moon(&error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_cheats(&field, cheat, &error);

	if (status == MH_ERROR_NONE) {
		saved = field;

		if (mh_field_set_cheats(&field,
			MH_CHEAT_SUPERCARS | MH_CHEAT_IGNITION,
			NULL) == MH_ERROR_NONE ||
			memcmp(&field, &saved, sizeof(field)) != 0)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"combined hidden modes accepted or changed field");
	}

	mh_race_scene_bind_cameras(&race_context.scene, &field);
	memset(&controls, 0, sizeof(controls));
	controls.throttle = .75;

	for (i = 0; i < 120 && status == MH_ERROR_NONE; i++) {
		if (i % 15 == 0)
			status = mh_field_cycle_view(&field, &error);

		if (status != MH_ERROR_NONE)
			break;

		viewed = mh_field_viewed(&field);
		request = (i / 30) % 3;
		status = mh_field_camera_mode(
			&field, (enum mh_camera_mode)request, &error);

		if (status != MH_ERROR_NONE)
			break;

		field.cars[viewed].camera_goal = i >= 90;
		field.cars[viewed].look_back = (i / 10) % 2;
		field.cars[viewed].camera_focal = 3;
		field.cars[viewed].camera_far = 125;
		prior = field.cars[viewed].camera;
		listener = field.listener_revision;
		controls.steering = i < 60 ? .2 : -.2;
		status = mh_field_advance(
			&field, &controls, elapsed[i % 5], 0, &steps, &error);

		if (status == MH_ERROR_NONE)
			status = mh_motion_display(
				&field.cars[viewed].vehicle.motion, position,
				velocity, &error);

		if (status != MH_ERROR_NONE)
			break;

		if (field.cars[viewed].camera_mode != MH_CAMERA_CHASE ||
			field.cars[viewed].camera_goal ||
			mh_drive_camera_in_car(&field.cars[viewed]) ||
			field.listener_revision != listener ||
			mh_field_listener_positional(&field) !=
				(request == MH_CAMERA_TRACK) ||
			field.cars[viewed].camera_focal != 1 ||
			field.cars[viewed].camera_far !=
				mh_drive_camera_far(&field.cars[viewed]) ||
			mh_drive_camera_look_back(&field.cars[viewed]) !=
				field.cars[viewed].look_back) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"hidden camera override changed view/lens/listener incorrectly");
			break;
		}

		mh_field_matrices(&field, 1.5, models, matrix, view);
		status = mh_race_scene_details(&race_context.scene, &field,
			models, 960, MH_RENDER_GLIDE, &error);

		for (j = 0; j < field.count && status == MH_ERROR_NONE; j++)
			if (race_context.scene.assets.meshes[1 + 5 * j].lod !=
					level ||
				!mh_race_scene_part_visible(&race_context.scene,
					&field, 1 + 5 * j) ||
				mh_race_scene_part_visible(
					&race_context.scene, &field, 2 + 5 * j))
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"hidden detail or wheel visibility differs after camera handoff");

		if (status != MH_ERROR_NONE)
			break;

		printf("%s %u %d %.9g", supercars ? "supercars" : "ignition",
			request, field.cars[viewed].look_back,
			(double)field.cars[0].frame.time.elapsed);

		for (j = 0; j < 3; j++)
			printf(" %.9g", (double)prior.position[j]);

		for (j = 0; j < 9; j++)
			printf(" %.9g",
				(double)(float)field.cars[viewed]
					.vehicle.motion.rotation[j]);

		for (j = 0; j < 3; j++)
			printf(" %.9g", (double)(float)position[j]);

		for (j = 0; j < 9; j++)
			printf(" %.9g",
				(double)field.cars[viewed].camera.rotation[j]);

		for (j = 0; j < 3; j++)
			printf(" %.9g",
				(double)field.cars[viewed].camera.position[j]);

		putchar('\n');
	}

	if (status == MH_ERROR_NONE)
		status = mh_field_reset(&field, 0, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_reset(&field, 1, &error);

	if (status == MH_ERROR_NONE)
		for (i = 0; i < field.count; i++)
			if (field.cars[i].cheats != cheat)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"reset lost hidden mode");

	if (capture != NULL && status == MH_ERROR_NONE) {
		field.cars[mh_field_viewed(&field)].look_back = 0;
		status = mh_hud_assets_load(argv[1], &race_context.hud, &error);

		if (status == MH_ERROR_NONE)
			status = mh_results_assets_load(argv[1], field.count,
				name, &race_context.hud.atlas,
				&race_context.results, &error);

		if (status == MH_ERROR_NONE && !capture_muted(argv[1], capture))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"hidden mode capture failed");
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE || ferror(stdout);
}
