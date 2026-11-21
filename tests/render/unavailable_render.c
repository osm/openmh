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

static enum mh_error_code
check_unavailable_controls(
	const struct mh_field *source, struct mh_error *error_out)
{
	static struct mh_field field_copy;
	struct mh_vehicle_state previous;
	struct mh_engine_input controls;
	struct mh_engine_options options;
	unsigned int manual, tick;
	enum mh_error_code status;

	for (manual = 0; manual < 2; manual++) {
		field_copy = *source;
		field_copy.network = 1;
		field_copy.active_mask = field_copy.unavailable_mask = 1;
		options = field_copy.cars[0].config.options;
		options.manual = (int)manual;
		status = mh_engine_configure(field_copy.cars[0].car, &options,
			&field_copy.cars[0].config, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		previous = field_copy.cars[0].vehicle;
		memset(&controls, 0, sizeof(controls));
		controls.throttle = .75;
		controls.steering = .5;

		for (tick = 0; tick < 120; tick++) {
			controls.shift_up = manual && tick < 60;
			status = mh_field_tick(
				&field_copy, &controls, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (memcmp(&field_copy.cars[0].vehicle.motion,
				&previous.motion,
				sizeof(previous.motion)) != 0 ||
				memcmp(&field_copy.cars[0].vehicle.grip,
					&previous.grip,
					sizeof(previous.grip)) != 0 ||
				memcmp(field_copy.cars[0].vehicle.wheel_rate,
					previous.wheel_rate,
					sizeof(previous.wheel_rate)) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"unavailable controls changed road state");
		}

		if (field_copy.cars[0].vehicle.smoothed_steering < .49 ||
			field_copy.cars[0].sound_pedal != .75 ||
			(manual && field_copy.cars[0].vehicle.engine.gear != 2))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"unavailable car did not update controls");

		field_copy.unavailable_mask = 0;
		memset(&controls, 0, sizeof(controls));
		status = mh_field_tick(&field_copy, &controls, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
capture_frame(struct mh_renderer *renderer, const char *prefix,
	const char *state, const float *matrix, const float *view,
	const float *models, const struct mh_projection *projection)
{
	char path[1024];
	size_t prefix_length, state_length;

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

	return (projection != NULL ? mh_renderer_scene_frame_lens(renderer,
					projection, view, models, 1, NULL,
					NULL, NULL, NULL) == MH_ERROR_NONE
				: mh_renderer_scene_frame(renderer, matrix,
					view, models, 1)) &&
		mh_renderer_capture(renderer, path);
}

static int
check_blend_frames(const char *prefix)
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
	unsigned int i, n, pass;
	int success;
	char path[1024];
	size_t prefix_length;

	prefix_length = strlen(prefix);

	if (prefix_length > sizeof(path) - sizeof("-blend-0"))
		return 0;

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
			vertices[n][i].position[2] = n == 0 ? .9f
				: n == 1		    ? .1f
							: .5f;
			vertices[n][i].color[n] = .2f;
		}
	}

	success = 1;

	/*
	 * Place opaque background between effect meshes in submission order.
	 * Effect order reverses between captures. Both must add to the opaque
	 * background; the near effect must not occlude the farther effect.
	 */
	for (pass = 0; pass < 2 && success; pass++) {
		list[0] = &meshes[pass ? 2 : 1];
		list[1] = &meshes[0];
		list[2] = &meshes[pass ? 1 : 2];
		renderer = mh_sdl_renderer_create_scene(NULL, list, 3);

		if (renderer == NULL)
			return 0;

		success = mh_renderer_mesh_unavailable(
				renderer, 0, list[0]->vertices) &&
			mh_renderer_mesh_unavailable(
				renderer, 2, list[2]->vertices);
		memcpy(path, prefix, prefix_length);
		memcpy(path + prefix_length, pass ? "-blend-1" : "-blend-0",
			sizeof("-blend-0"));

		if (success)
			success = capture_frame(renderer, path, "visible",
				identity, identity, NULL, NULL);

		/* Opaque foreground blocks both additive surfaces. */
		for (i = 0; i < 6; i++)
			vertices[0][i].position[2] = 0;

		if (success)
			success = mh_renderer_update_mesh(renderer, 1) &&
				capture_frame(renderer, path, "occluded",
					identity, identity, NULL, NULL);

		for (i = 0; i < 6; i++)
			vertices[0][i].position[2] = .9f;

		mh_renderer_destroy(renderer);
	}

	return success;
}

static int
restore_direct3d_lighting(
	struct mh_renderer *renderer, const float *models, const float view[16])
{
	struct mh_error error;
	unsigned int i;

	for (i = 1; i < 6; i++) {
		if (mh_race_scene_shade_car(&race_context.scene, &field, i,
			models, view, 0, 1, &error) != MH_ERROR_NONE) {
			fprintf(stderr, "Available lighting: %s\n",
				error.message);
			return 0;
		}

		if (!mh_renderer_mesh_lit(renderer, i,
			race_context.scene.visuals.shaded_vertices[i]))
			return 0;
	}

	return 1;
}

int
main(int argc, char *argv[])
{
	const struct mh_mesh *meshes[6];
	struct mh_drive drive;
	struct mh_camera_lens lens;
	struct mh_projection projection;
	const struct mh_projection *scene_projection;
	struct mh_engine_input controls;
	struct mh_renderer *renderer;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int car, track, i, k;
	size_t n, visible, total;
	int success, capture_enabled, direct3d;

	if (argc < 4 || argc > 6 || (argc >= 5 && strlen(argv[4]) > 950) ||
		(argc == 6 && strcmp(argv[5], "--direct3d") != 0)) {
		fprintf(stderr,
			"Usage: mh-unavailable-render GAME_ROOT CAR TRACK [CAPTURE_PREFIX [--direct3d]]\n");
		return 2;
	}

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);
	capture_enabled = argc >= 5;
	direct3d = argc == 6;
	scene_projection = NULL;

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	status = mh_race_context_load_profile(argv[1], car, track, 0, NULL,
		direct3d ? MH_RENDER_DIRECT3D : MH_RENDER_GLIDE, NULL,
		&race_context, &error);

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

	if (status == MH_ERROR_NONE)
		status = check_unavailable_controls(&field, &error);

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

	visible = total = 0;

	for (i = 1; i < 6; i++) {
		status = mh_race_scene_shade_unavailable(
			&race_context.scene, &field, i, models, view, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		for (n = 0; n < race_context.scene.assets.meshes[i].face_count;
			n++) {
			const struct mh_mesh_face *face;
			const struct mh_vertex *a, *b, *c;
			size_t v;

			face = &race_context.scene.assets.meshes[i].faces[n];
			a = race_context.scene.visuals.shaded_vertices[i] +
				face->first;
			b = a + 1;
			c = a + 2;
			total++;

			if (memcmp(a->position, b->position,
				sizeof(a->position)) == 0 &&
				memcmp(a->position, c->position,
					sizeof(a->position)) == 0)
				continue;

			visible++;

			for (v = 0; v < face->count; v++)
				for (k = 0; k < 3; k++) {
					if (!(a[v].color[k] >= 5.0f / 255 &&
						a[v].color[k] <=
							70.0f / 255)) {
						status = mh_error_set(&error,
							MH_ERROR_FAILED,
							"unavailable vertex outside original bounds");
						goto done;
					}
				}
		}
	}

	if (visible == 0 || visible == total) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"real car plane visibility did not split faces");
		goto done;
	}

	printf("Car %u track %u: %lu of %lu faces visible, all five parts shaded\n",
		car, track, (unsigned long)visible, (unsigned long)total);

	if (!capture_enabled)
		goto done;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		status = mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		goto done;
	}

	success = check_blend_frames(argv[4]);

	for (i = 0; i < 6; i++)
		meshes[i] = &race_context.scene.assets.meshes[i];

	renderer = mh_sdl_renderer_create_scene(NULL, meshes, 6);

	if (renderer == NULL)
		success = 0;

	if (success && direct3d)
		success = restore_direct3d_lighting(renderer, models, view);

	if (success)
		success = capture_frame(renderer, argv[4], "normal", matrix,
			view, models, scene_projection);

	for (i = 1; i < 6 && success; i++) {
		if (direct3d &&
			mh_race_scene_shade_unavailable(&race_context.scene,
				&field, i, models, view,
				&error) != MH_ERROR_NONE) {
			fprintf(stderr, "Unavailable lighting: %s\n",
				error.message);
			success = 0;
			break;
		}

		success = mh_renderer_mesh_unavailable(renderer, i,
			race_context.scene.visuals.shaded_vertices[i]);
	}

	if (success)
		success = capture_frame(renderer, argv[4], "unavailable",
			matrix, view, models, scene_projection);

	if (success && direct3d)
		success = restore_direct3d_lighting(renderer, models, view);
	else
		for (i = 1; i < 6 && success; i++)
			success =
				mh_renderer_mesh_unavailable(renderer, i, NULL);

	if (success)
		success = capture_frame(renderer, argv[4], "restored", matrix,
			view, models, scene_projection);

	for (i = 1; i < 6 && success; i++)
		success = mh_renderer_mesh_visible(renderer, i, 0);

	if (success)
		success = capture_frame(renderer, argv[4], "hidden", matrix,
			view, models, scene_projection);

	if (!success)
		fprintf(stderr, "GPU: %s\n", SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (!success)
		status = mh_error_set(
			&error, MH_ERROR_FAILED, "unavailable capture failed");

done:
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
