#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/camera.h"

static struct mh_race_context race_context;
static struct mh_field field;
static struct mh_skids saved[MH_RACE_MAX_CARS];

static int
capture_skids(struct mh_renderer *renderer, const char *prefix,
	const char *state, const float *matrix, const float *view,
	const float *models, const struct mh_projection *projection)
{
	char path[1024];
	size_t prefix_length, state_length;
	int success;

	prefix_length = strlen(prefix);
	state_length = strlen(state);

	/* Include separator, extension and terminating NUL in the bound. */
	if (prefix_length > sizeof(path) - 6 ||
		state_length > sizeof(path) - 6 - prefix_length) {
		fprintf(stderr, "Capture filename is too long\n");
		return 0;
	}

	memcpy(path, prefix, prefix_length);
	path[prefix_length] = '-';
	memcpy(path + prefix_length + 1, state, state_length);
	memcpy(path + prefix_length + 1 + state_length, ".bmp", 5);

	if (projection != NULL)
		success = mh_renderer_scene_frame_lens(renderer, projection,
				view, models, 1, NULL, NULL, NULL,
				NULL) == MH_ERROR_NONE;
	else
		success = mh_renderer_scene_frame(
			renderer, matrix, view, models, 1);

	return success && mh_renderer_capture(renderer, path);
}

static int
capture_blend(const char *prefix)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	struct mh_vertex vertices[3][6];
	struct mh_mesh meshes[3];
	const struct mh_mesh *list[3];
	struct mh_draw draw;
	struct mh_image white;
	struct mh_renderer *renderer;
	unsigned char pixel[4] = {255, 255, 255, 255};
	unsigned int i, n, j;
	int success;

	memset(vertices, 0, sizeof(vertices));
	memset(meshes, 0, sizeof(meshes));
	memset(&draw, 0, sizeof(draw));
	draw.count = 6;
	draw.prelit = 1;
	white.rgba = pixel;
	white.width = white.height = 1;

	for (n = 0; n < 3; n++) {
		meshes[n].vertices = vertices[n];
		meshes[n].vertex_count = 6;
		meshes[n].draws = &draw;
		meshes[n].draw_count = 1;
		meshes[n].textures = &white;
		meshes[n].texture_count = 1;

		for (i = 0; i < 6; i++) {
			vertices[n][i].position[0] = xy[i][0];
			vertices[n][i].position[1] = xy[i][1];

			if (n == 0)
				vertices[n][i].position[2] = .9f;
			else if (n == 1)
				vertices[n][i].position[2] = .1f;
			else
				vertices[n][i].position[2] = .5f;

			for (j = 0; j < 3; j++)
				vertices[n][i].color[j] =
					n == 0 ? .5f : 216.0f / 255.0f;
		}
	}

	/* Opaque background deliberately submitted between near/far marks. */
	list[0] = &meshes[1];
	list[1] = &meshes[0];
	list[2] = &meshes[2];
	renderer = mh_sdl_renderer_create_scene(NULL, list, 3);

	if (renderer == NULL)
		return 0;

	success = mh_renderer_mesh_visible(renderer, 0, 0) &&
		mh_renderer_mesh_visible(renderer, 2, 0) &&
		capture_skids(renderer, prefix, "blend-base", identity,
			identity, NULL, NULL);

	if (success)
		success = mh_renderer_mesh_skids(renderer, 0) &&
			mh_renderer_mesh_skids(renderer, 2) &&
			mh_renderer_mesh_visible(renderer, 0, 1) &&
			capture_skids(renderer, prefix, "blend-one", identity,
				identity, NULL, NULL);

	if (success)
		success = mh_renderer_mesh_visible(renderer, 2, 1) &&
			capture_skids(renderer, prefix, "blend-two", identity,
				identity, NULL, NULL);

	/* The farther ribbon must multiply too; neither mark writes depth. */
	for (i = 0; i < 6; i++)
		vertices[0][i].position[2] = 0;

	if (success)
		success = mh_renderer_update_mesh(renderer, 1) &&
			capture_skids(renderer, prefix, "blend-occluded",
				identity, identity, NULL, NULL);

	mh_renderer_destroy(renderer);

	return success;
}

int
main(int argc, char *argv[])
{
	const struct mh_mesh *meshes[7];
	struct mh_drive drive;
	struct mh_camera_lens lens;
	struct mh_projection projection;
	const struct mh_projection *scene_projection;
	struct mh_engine_input controls;
	struct mh_renderer *renderer;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int car, track, i, j, total, maximum, supported;
	size_t n, vertices;
	int success, render, direct3d;
	double origin;

	if (argc < 4 || argc > 6 || (argc >= 5 && strlen(argv[4]) > 950) ||
		(argc == 6 && strcmp(argv[5], "--direct3d") != 0)) {
		fprintf(stderr,
			"Usage: mh-skids-assets GAME_ROOT CAR TRACK [CAPTURE_PREFIX [--direct3d]]\n");
		return 2;
	}

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);
	render = argc >= 5;
	direct3d = argc == 6;
	scene_projection = NULL;

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

	memset(&controls, 0, sizeof(controls));

	for (i = 0; i < 120 && status == MH_ERROR_NONE; i++)
		status = mh_field_tick(&field, &controls, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	/*
	 * Give the settled car a reproducible approach speed; actual engine,
	 * suspension, grip, brake and steering generate every subsequent mark.
	 */
	field.cars[0].vehicle.motion.velocity[2] = 35;
	field.cars[0].vehicle.motion.previous_velocity[2] = 35;
	field.cars[0].vehicle.engine.gear = 3;
	controls.brake = 1;
	controls.steering = .35;
	maximum = supported = 0;

	for (i = 0; i < 240 && status == MH_ERROR_NONE; i++) {
		origin = field.cars[0].vehicle.motion.position[1];
		status = mh_field_tick(&field, &controls, &error);

		if (status != MH_ERROR_NONE)
			break;

		if (field.cars[0].recovery_event == MH_RECOVERY_NONE &&
			field.cars[0].vehicle.contact_origin_y != origin) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"lost tick-entry skid height");
			break;
		}

		for (j = 0; j < 4; j++)
			if (field.cars[0].vehicle.grip.wheels[j].supported) {
				if (!field.cars[0]
						.vehicle.wheel_contacts[j]
						.hit) {
					status = mh_error_set(&error,
						MH_ERROR_FAILED,
						"lost final wheel contact");
					break;
				}

				supported++;
			}

		if (status != MH_ERROR_NONE)
			break;

		if (i % 2 == 0)
			status = mh_race_scene_skids(
				&race_context.scene, &field, &error);

		total = 0;

		for (j = 0; j < 4; j++)
			total += race_context.scene.visuals.skids[0]
					.wheels[j]
					.count;

		if (total > maximum)
			maximum = total;
	}

	if (status != MH_ERROR_NONE)
		goto done;

	if (!maximum || !supported) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"real braking did not generate supported tyre marks");
		goto done;
	}

	memcpy(saved, race_context.scene.visuals.skids, sizeof(saved));
	field.network = 1;
	field.unavailable_mask = 1;
	status = mh_race_scene_skids(&race_context.scene, &field, &error);

	if (status == MH_ERROR_NONE &&
		memcmp(saved, race_context.scene.visuals.skids,
			sizeof(saved)) != 0)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"unavailable car changed mark history");

	field.unavailable_mask = 0;
	field.active_mask = 0;

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_skids(
			&race_context.scene, &field, &error);

	if (status == MH_ERROR_NONE &&
		memcmp(saved, race_context.scene.visuals.skids,
			sizeof(saved)) != 0)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"departed car erased road marks");

	field.network = 0;
	field.active_mask = 1;

	if (status != MH_ERROR_NONE)
		goto done;

	mh_field_matrices(&field, (double)MH_VIEW_WIDTH / MH_VIEW_HEIGHT,
		models, matrix, view);

	if (direct3d) {
		status = mh_camera_lens_setup(MH_CAMERA_RACE_FOCAL,
			MH_CAMERA_RACE_NEAR,
			mh_drive_camera_far(&field.cars[0]),
			(float)MH_VIEW_WIDTH / MH_VIEW_HEIGHT, &lens, &error);

		if (status == MH_ERROR_NONE)
			status = mh_projection_setup(MH_VIEW_WIDTH,
				MH_VIEW_HEIGHT,
				(float)MH_VIEW_WIDTH / MH_VIEW_HEIGHT,
				lens.focal, lens.near_depth, lens.far_depth,
				&projection, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		scene_projection = &projection;
	}

	memcpy(models + 6 * 16, models, 16 * sizeof(float));
	status = mh_skid_mesh_update(&race_context.scene.visuals.skid_mesh,
		race_context.scene.visuals.skids, MH_RACE_MAX_CARS, matrix,
		field.cars[0].eye, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	vertices = 0;

	for (n = 0; n < race_context.scene.visuals.skid_mesh.vertex_count; n++)
		if (race_context.scene.visuals.skid_mesh.vertices[n].color[0] >
			0)
			vertices++;

	printf("car %u track %u: %u completed marks, %u supported wheel ticks, %lu ribbon vertices\n",
		car, track, maximum, supported, (unsigned long)vertices);

	if (render) {
		SDL_SetHintWithPriority(
			SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

		if (!SDL_Init(SDL_INIT_VIDEO)) {
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
			goto done;
		}

		success = capture_blend(argv[4]);

		for (i = 0; i < 6; i++)
			meshes[i] = &race_context.scene.assets.meshes[i];

		meshes[6] = &race_context.scene.visuals.skid_mesh;
		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 7);

		if (renderer == NULL)
			success = 0;

		if (success)
			success = mh_renderer_mesh_visible(renderer, 6, 0) &&
				capture_skids(renderer, argv[4], "clean",
					matrix, view, models, scene_projection);

		if (success)
			success = mh_renderer_mesh_skids(renderer, 6) &&
				mh_renderer_mesh_visible(renderer, 6, 1) &&
				capture_skids(renderer, argv[4], "marked",
					matrix, view, models, scene_projection);

		if (!success)
			fprintf(stderr, "GPU: %s\n", SDL_GetError());

		mh_renderer_destroy(renderer);
		SDL_Quit();

		if (!success) {
			status = mh_error_set(
				&error, MH_ERROR_FAILED, "skid capture failed");
			goto done;
		}
	}

	status = mh_field_reset(&field, 1, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_skids(
			&race_context.scene, &field, &error);

	if (status == MH_ERROR_NONE)
		for (i = 0; i < MH_RACE_MAX_CARS; i++)
			for (j = 0; j < 4; j++)
				if (race_context.scene.visuals.skids[i]
						.wheels[j]
						.count ||
					race_context.scene.visuals.skids[i]
						.wheels[j]
						.partial)
					status = mh_error_set(&error,
						MH_ERROR_FAILED,
						"race restart retained skid history");

done:
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
