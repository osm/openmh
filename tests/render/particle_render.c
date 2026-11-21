#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/drive_particles.h"

static struct mh_race_context race_context;
static struct mh_field field, best_field;
static struct mh_particles best_particles[MH_RACE_MAX_CARS],
	saved_particles[MH_RACE_MAX_CARS];
static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
capture_frame(struct mh_renderer *renderer, const char *prefix,
	const char *name, const float *matrix, const float *view,
	const float *models)
{
	char path[1024];

	sprintf(path, "%s-%s.bmp", prefix, name);

	return mh_renderer_scene_frame(renderer, matrix, view, models, 1) &&
		mh_renderer_capture(renderer, path);
}

static int
check_blend_frames(const char *prefix)
{
	static const float xy[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

	struct mh_vertex vertices[5][6];
	struct mh_mesh meshes[5];
	const struct mh_mesh *list[5];
	struct mh_draw draws[5];
	struct mh_image textures[2];
	struct mh_renderer *renderer;
	float shifted[16];
	unsigned char pixels[2][4] = {
		{255, 255, 255, 255}, {128, 64, 192, 255}};

	unsigned int i, n, j;
	int success;

	memset(vertices, 0, sizeof(vertices));
	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));

	for (i = 0; i < 2; i++) {
		textures[i].rgba = pixels[i];
		textures[i].width = textures[i].height = 1;
	}

	for (n = 0; n < 5; n++) {
		meshes[n].vertices = vertices[n];
		meshes[n].vertex_count = 6;
		meshes[n].draws = &draws[n];
		meshes[n].draw_count = 1;
		meshes[n].textures = textures;
		meshes[n].texture_count = 2;
		draws[n].count = 6;
		draws[n].prelit = 1;
		draws[n].texture = n == 1 || n == 2 ? 1 : 0;

		for (i = 0; i < 6; i++) {
			vertices[n][i].position[0] =
				xy[i][0] * (n == 0 ? 4 : .3f);
			vertices[n][i].position[1] =
				xy[i][1] * (n == 0 ? 4 : .3f);
			vertices[n][i].position[2] = n == 0 ? .9f
				: n == 2		    ? .5f
							: .1f;

			for (j = 0; j < 3; j++)
				vertices[n][i].color[j] = n == 0
					? (float)(32 + j * 16) / 255
					: n == 4 ? (float)(16 + j * 16) / 255
						: .5f;
		}
	}

	/*
	 * Reverse material order: unavailable, particle, mark, opaque,
	 * particle.
	 */
	list[0] = &meshes[4];
	list[1] = &meshes[1];
	list[2] = &meshes[3];
	list[3] = &meshes[0];
	list[4] = &meshes[2];
	renderer = mh_sdl_renderer_create_scene(NULL, list, 5);

	if (renderer == NULL)
		return 0;

	success = mh_renderer_mesh_visible(renderer, 0, 0) &&
		mh_renderer_mesh_visible(renderer, 1, 0) &&
		mh_renderer_mesh_visible(renderer, 2, 0) &&
		mh_renderer_mesh_visible(renderer, 4, 0) &&
		capture_frame(renderer, prefix, "blend-base", identity,
			identity, NULL);

	if (success)
		success = mh_renderer_mesh_particles(renderer, 1) &&
			mh_renderer_mesh_particles(renderer, 4) &&
			mh_renderer_mesh_visible(renderer, 1, 1) &&
			capture_frame(renderer, prefix, "blend-one", identity,
				identity, NULL);

	memcpy(shifted, identity, sizeof(shifted));
	shifted[12] = .7f;

	if (success)
		success = capture_frame(renderer, prefix, "blend-shifted",
			shifted, identity, NULL);

	if (success)
		success = mh_renderer_mesh_visible(renderer, 4, 1) &&
			capture_frame(renderer, prefix, "blend-two", identity,
				identity, NULL);

	if (success)
		success = mh_renderer_mesh_skids(renderer, 2) &&
			mh_renderer_mesh_unavailable(
				renderer, 0, vertices[4]) &&
			mh_renderer_mesh_visible(renderer, 0, 1) &&
			mh_renderer_mesh_visible(renderer, 2, 1) &&
			capture_frame(renderer, prefix, "blend-ordered",
				identity, identity, NULL);

	for (i = 0; i < 6; i++)
		vertices[0][i].position[2] = 0;

	if (success)
		success = mh_renderer_update_mesh(renderer, 3) &&
			capture_frame(renderer, prefix, "blend-occluded",
				identity, identity, NULL);

	mh_renderer_destroy(renderer);

	return success;
}

static enum mh_error_code
check_particle_lifecycle(struct mh_error *error_out)
{
	struct mh_particle_system old_system;
	unsigned int i, count;
	float remaining;
	enum mh_error_code status;

	mh_race_scene_reset_particles(&race_context.scene);
	field.scene_seconds = 0;
	field.active_mask = 1;
	field.unavailable_mask = 0;
	memset(&field.cars[0].body_effects, 0,
		sizeof(field.cars[0].body_effects));
	memset(&field.cars[0].body_sparks, 0,
		sizeof(field.cars[0].body_sparks));
	field.cars[0].vehicle.grip.tyre_effects.id[0] = 1;
	field.cars[0].vehicle.grip.tyre_effects.id[1] = 2;

	for (i = 0; i < 2; i++) {
		field.scene_seconds += .025;
		status = mh_race_scene_update_particles(
			&race_context.scene, &field, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (race_context.scene.visuals.particles[0].pools[0].count != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"emitted before timer threshold");
	}

	field.scene_seconds += .025;
	status = mh_race_scene_update_particles(
		&race_context.scene, &field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race_context.scene.visuals.particles[0].pools[0].count != 1 ||
		race_context.scene.visuals.particles[0].pools[1].count != 1 ||
		race_context.scene.visuals.particle_timer != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"missing threshold emission or discarded-frame timing");

	/* Zero elapsed at a reset timer preserves pools and random stream. */
	memcpy(saved_particles, race_context.scene.visuals.particles,
		sizeof(saved_particles));
	old_system = race_context.scene.visuals.particle_system;
	status = mh_race_scene_update_particles(
		&race_context.scene, &field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (memcmp(saved_particles, race_context.scene.visuals.particles,
		sizeof(saved_particles)) ||
		memcmp(&old_system, &race_context.scene.visuals.particle_system,
			sizeof(old_system)))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"paused scene changed particle state");

	remaining = race_context.scene.visuals.particles[0]
			.pools[0]
			.particles[0]
			.remaining;
	count = race_context.scene.visuals.particles[0].pools[0].count;
	field.network = 1;
	field.unavailable_mask = 1;

	for (i = 0; i < 3; i++) {
		field.scene_seconds += .025;
		status = mh_race_scene_update_particles(
			&race_context.scene, &field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (race_context.scene.visuals.particles[0].pools[0].count != count ||
		race_context.scene.visuals.particles[0]
				.pools[0]
				.particles[0]
				.remaining >= remaining ||
		memcmp(&old_system, &race_context.scene.visuals.particle_system,
			sizeof(old_system)))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unavailable car emitted or failed to age");

	remaining = race_context.scene.visuals.particles[0]
			.pools[0]
			.particles[0]
			.remaining;
	field.unavailable_mask = 0;
	field.active_mask = 0;

	for (i = 0; i < 3; i++) {
		field.scene_seconds += .025;
		status = mh_race_scene_update_particles(
			&race_context.scene, &field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (race_context.scene.visuals.particles[0].pools[0].count != count ||
		race_context.scene.visuals.particles[0]
				.pools[0]
				.particles[0]
				.remaining >= remaining)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"departed car failed to retain and age particles");

	/*
	 * Failed eligible emission must not partially advance any pool or RNG.
	 */
	field.active_mask = 1;
	race_context.scene.visuals.particle_timer = .06f;
	field.cars[0].remote = 1;
	field.cars[0].body_sparks.corner[1] = MH_CAR_BODY_POINTS;
	memcpy(saved_particles, race_context.scene.visuals.particles,
		sizeof(saved_particles));
	old_system = race_context.scene.visuals.particle_system;

	if (mh_race_scene_update_particles(&race_context.scene, &field, NULL) ==
			MH_ERROR_NONE ||
		memcmp(saved_particles, race_context.scene.visuals.particles,
			sizeof(saved_particles)) ||
		memcmp(&old_system, &race_context.scene.visuals.particle_system,
			sizeof(old_system)) ||
		race_context.scene.visuals.particle_timer != .06f)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"failed frame changed particle state");

	field.cars[0].body_sparks.corner[1] = 0;
	field.cars[0].remote = 0;
	field.network = 0;
	race_context.scene.visuals.particle_timer = .04f;
	status = mh_field_reset(&field, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_update_particles(
			&race_context.scene, &field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (memcmp(saved_particles, race_context.scene.visuals.particles,
		sizeof(saved_particles)) ||
		memcmp(&old_system, &race_context.scene.visuals.particle_system,
			sizeof(old_system)) ||
		race_context.scene.visuals.particle_timer != .04f)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"restart lost particle history");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	const struct mh_mesh *meshes[7], *particle_mesh;
	struct mh_drive drive;
	struct mh_engine_input controls;
	struct mh_particle_camera camera;
	struct mh_renderer *renderer;
	float models[(MH_FIELD_MESHES + 1) * 16], view[16];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int car, track, i;
	size_t vertex, visible_vertices, maximum;
	int success;

	if ((argc != 4 && argc != 5) || (argc == 5 && strlen(argv[4]) > 950)) {
		fprintf(stderr,
			"Usage: mh-particle-render GAME_ROOT CAR TRACK [CAPTURE_PREFIX]\n");
		return 2;
	}

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	status = mh_race_context_load(
		argv[1], car, track, 0, &race_context, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	status = mh_drive_start(&race_context.scene.assets.car,
		&race_context.scene.assets.world_collision,
		&race_context.scene.assets.materials,
		&race_context.scene.assets.route,
		&race_context.scene.assets.track, 0, 0, 0, &drive, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, field.count,
			&error);

	if (status != MH_ERROR_NONE)
		goto done;

	memset(&controls, 0, sizeof(controls));
	memset(&camera, 0, sizeof(camera));
	camera.near_depth = MH_CAMERA_RACE_NEAR;
	camera.far_depth = mh_drive_camera_far(&drive);
	camera.width = MH_VIEW_WIDTH;
	camera.height = MH_VIEW_HEIGHT;
	camera.brightness = 1;
	camera.smoke = camera.sparks = 1;

	for (i = 0; i < 120 && status == MH_ERROR_NONE; i++) {
		status = mh_field_frame_tick_duration(&field, &controls,
			i % 2 == 0, i % 2 == 0 ? 2 * MH_DRIVE_DT : 0, &error);

		if (status == MH_ERROR_NONE && i % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, &error);

		if (status == MH_ERROR_NONE && i % 2)
			status = mh_race_scene_update_particles(
				&race_context.scene, &field, &error);
	}

	if (status != MH_ERROR_NONE)
		goto done;

	field.cars[0].vehicle.motion.velocity[2] =
		field.cars[0].vehicle.motion.previous_velocity[2] = 35;
	field.cars[0].vehicle.engine.gear = 3;
	controls.brake = 1;
	controls.steering = .35;
	maximum = 0;

	for (i = 0; i < 120 && status == MH_ERROR_NONE; i++) {
		status = mh_field_frame_tick_duration(&field, &controls,
			i % 2 == 0, i % 2 == 0 ? 2 * MH_DRIVE_DT : 0, &error);

		if (status == MH_ERROR_NONE && i % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, &error);

		if (status != MH_ERROR_NONE || i % 2 == 0)
			continue;

		status = mh_race_scene_update_particles(
			&race_context.scene, &field, &error);
		mh_field_matrices(&field,
			(double)MH_VIEW_WIDTH / MH_VIEW_HEIGHT, models,
			camera.matrix, view);

		if (status == MH_ERROR_NONE)
			status = mh_particle_mesh_update(
				&race_context.scene.visuals.particle_mesh,
				race_context.scene.visuals.particles,
				MH_RACE_MAX_CARS, &camera, &error);

		if (status != MH_ERROR_NONE)
			break;

		particle_mesh = &race_context.scene.visuals.particle_mesh;
		visible_vertices = 0;

		for (vertex = 0; vertex < particle_mesh->vertex_count; vertex++)
			if (particle_mesh->vertices[vertex].color[0] > 0)
				visible_vertices++;

		if (visible_vertices > maximum) {
			maximum = visible_vertices;
			best_field = field;
			memcpy(best_particles,
				race_context.scene.visuals.particles,
				sizeof(best_particles));
		}
	}

	if (status != MH_ERROR_NONE)
		goto done;

	if (maximum == 0) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"actual braking produced no visible particles");
		goto done;
	}

	printf("car%02u track%u: %lu visible braking vertices; ", car, track,
		(unsigned long)maximum);
	status = check_particle_lifecycle(&error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("cadence, paused clock, unavailable/departed pools, atomic failure and restart passed\n");

	if (argc == 5) {
		field = best_field;
		memcpy(race_context.scene.visuals.particles, best_particles,
			sizeof(best_particles));
		mh_field_matrices(&field,
			(double)MH_VIEW_WIDTH / MH_VIEW_HEIGHT, models,
			camera.matrix, view);
		memcpy(models + 6 * 16, identity, sizeof(identity));
		status = mh_particle_mesh_update(
			&race_context.scene.visuals.particle_mesh,
			race_context.scene.visuals.particles, MH_RACE_MAX_CARS,
			&camera, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		SDL_SetHintWithPriority(
			SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

		if (!SDL_Init(SDL_INIT_VIDEO)) {
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
			goto done;
		}

		success = check_blend_frames(argv[4]);

		for (i = 0; i < 6; i++)
			meshes[i] = &race_context.scene.assets.meshes[i];

		meshes[6] = &race_context.scene.visuals.particle_mesh;
		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 7);

		if (renderer == NULL)
			success = 0;

		if (success)
			success = mh_renderer_mesh_visible(renderer, 6, 0) &&
				capture_frame(renderer, argv[4], "clean",
					camera.matrix, view, models);

		if (success)
			success = mh_renderer_mesh_particles(renderer, 6) &&
				mh_renderer_mesh_visible(renderer, 6, 1) &&
				capture_frame(renderer, argv[4], "smoke",
					camera.matrix, view, models);

		/*
		 * Supplied received channels isolate grass/dust/spark
		 * presentation with the real CAR attachment geometry and shared
		 * atlas.
		 */
		memset(race_context.scene.visuals.particles, 0,
			sizeof(race_context.scene.visuals.particles));
		field.cars[0].remote = 1;
		field.cars[0].vehicle.grip.tyre_effects.id[0] = 2;
		field.cars[0].vehicle.grip.tyre_effects.id[1] = 3;

		for (i = 0; i < 2; i++) {
			field.cars[0].body_sparks.id[i] = 1;
			field.cars[0].body_sparks.gain[i] = 1;
			field.cars[0].body_sparks.corner[i] = i;
		}

		if (success)
			status = mh_drive_particles_emit(&field.cars[0],
				&race_context.scene.visuals.particle_system,
				&race_context.scene.visuals.particles[0],
				&error);

		if (success && status == MH_ERROR_NONE)
			status = mh_particle_mesh_update(
				&race_context.scene.visuals.particle_mesh,
				race_context.scene.visuals.particles,
				MH_RACE_MAX_CARS, &camera, &error);

		if (success)
			success = status == MH_ERROR_NONE &&
				mh_renderer_mesh_particles(renderer, 6) &&
				capture_frame(renderer, argv[4], "effects",
					camera.matrix, view, models);

		if (!success)
			fprintf(stderr, "GPU: %s\n", SDL_GetError());

		mh_renderer_destroy(renderer);
		SDL_Quit();

		if (!success)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"particle capture failed");
	}

done:
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
