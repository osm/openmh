#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"

static struct mh_race_context race_context;
static struct mh_field field;
static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static enum mh_error_code
check_name_frame(float *models, const float *view, int enabled,
	unsigned int expected, struct mh_error *error_out)
{
	const struct mh_vertex *vertex;
	enum mh_error_code status;
	size_t i;

	status = mh_race_scene_names(&race_context.scene, &field, models, view,
		960, 640, enabled, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race_context.scene.visuals.names.visible_mask != expected)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "name eligibility differs");

	for (i = 0; i < race_context.scene.visuals.names.mesh.vertex_count;
		i++) {
		vertex = &race_context.scene.visuals.names.mesh.vertices[i];

		if (vertex->projection_w != 0 &&
			!(vertex->position[2] > .001f &&
				vertex->position[2] < .0011f))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"wrong name depth conversion");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_name_policies(float *models, float *view, struct mh_error *error_out)
{
	unsigned int i, j;
	enum mh_error_code status;

	memcpy(view, identity, 16 * sizeof(float));
	view[10] = -1;

	for (i = 0; i < field.count; i++) {
		field.cars[i].camera_goal = 0;
		field.cars[i].camera_mode = MH_CAMERA_CHASE;
		memset(&field.cars[i].camera, 0, sizeof(field.cars[i].camera));
		field.cars[i].camera.rotation[0] =
			field.cars[i].camera.rotation[4] =
				field.cars[i].camera.rotation[8] = 1;

		for (j = 0; j < 5; j++)
			memcpy(models + (1 + 5 * i + j) * 16, identity,
				16 * sizeof(float));

		models[(1 + 5 * i) * 16 + 12] = (float)((int)i * 2 - 7);
		models[(1 + 5 * i) * 16 + 14] = 20;
	}

	status = check_name_frame(models, view, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 254, error_out);

	field.unavailable_mask = 255;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 254, error_out);

	field.viewed_entry = 3;
	field.cars[3].camera_mode = MH_CAMERA_IN_CAR;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 247, error_out);

	field.cars[3].camera_roll.external = 1;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 254, error_out);

	field.cars[3].camera_roll.external = 0;
	field.cars[3].camera_goal = 1;
	field.cars[3].camera_focal = 1;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 255, error_out);

	field.cars[3].camera_goal = 0;
	field.cars[3].camera_mode = MH_CAMERA_TRACK;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 255, error_out);

	field.active_mask = 0x5d;

	if (status == MH_ERROR_NONE)
		status = check_name_frame(models, view, 1, 0x5d, error_out);

	field.active_mask = 255;

	/*
	 * A sphere can fail while its elevated label passes point projection.
	 * The original wide-screen frustum remains a separate earlier gate.
	 */
	for (i = 0; i < 8; i++) {
		models[(1 + 5 * i) * 16 + 12] = 0;
		models[(1 + 5 * i) * 16 + 13] = -4;
		models[(1 + 5 * i) * 16 + 14] = 10;
		race_context.scene.assets.meshes[1 + 5 * i].radius = 0;
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_names(&race_context.scene, &field,
			models, view, 3840, 720, 1, error_out);

	if (status == MH_ERROR_NONE &&
		race_context.scene.visuals.names.visible_mask != 0)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"names bypassed object sphere culling");

	for (i = 0; i < 8; i++) {
		models[(1 + 5 * i) * 16 + 12] = (float)((int)i * 2 - 7);
		models[(1 + 5 * i) * 16 + 13] = 0;
		models[(1 + 5 * i) * 16 + 14] = 20;
	}

	if (status != MH_ERROR_NONE)
		return status;

	return check_name_frame(models, view, 1, 255, error_out);
}

static int
check_capture_pixels(const char *path, int visible)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel, *background;
	unsigned int x, y, changed;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	background = (const unsigned char *)rgba->pixels;
	changed = 0;

	for (y = 0; y < (unsigned int)rgba->h; y++)
		for (x = 0; x < (unsigned int)rgba->w; x++) {
			pixel = (const unsigned char *)rgba->pixels +
				y * rgba->pitch + x * 4;

			if (memcmp(pixel, background, 3) != 0)
				changed++;
		}

	SDL_DestroySurface(rgba);

	return visible ? changed > 500 : changed == 0;
}

static int
capture_names(const char *path, float *models, float *view)
{
	const struct mh_mesh *mesh;
	struct mh_renderer *renderer;
	char off[1024];
	unsigned int i;
	int success;

	if (strlen(path) > 900)
		return 0;

	for (i = 0; i < 8; i++) {
		models[(1 + 5 * i) * 16 + 12] = (float)(((int)i % 3 - 1) * 6);
		models[(1 + 5 * i) * 16 + 13] =
			(float)((1 - (int)i / 3) * 3 - 2);
		models[(1 + 5 * i) * 16 + 14] = 12;
	}

	if (check_name_frame(models, view, 1, 255, NULL) != MH_ERROR_NONE)
		return 0;

	mesh = &race_context.scene.visuals.names.mesh;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 0;

	renderer = mh_sdl_renderer_create_scene(NULL, &mesh, 1);
	success = renderer != NULL && mh_renderer_mesh_halos(renderer, 0) &&
		mh_renderer_scene_frame(renderer, identity, view, models, 0) &&
		mh_renderer_capture(renderer, path) &&
		check_capture_pixels(path, 1);
	sprintf(off, "%s-off.bmp", path);

	if (success)
		success = check_name_frame(models, view, 0, 0, NULL) ==
				MH_ERROR_NONE &&
			mh_renderer_mesh_halos(renderer, 0) &&
			mh_renderer_scene_frame(
				renderer, identity, view, models, 0) &&
			mh_renderer_capture(renderer, off) &&
			check_capture_pixels(off, 0);

	mh_renderer_destroy(renderer);
	SDL_Quit();

	return success;
}

int
main(int argc, char *argv[])
{
	struct mh_drive drive;
	const struct mh_car *roster[7];
	struct mh_error error;
	enum mh_error_code status;
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	unsigned int car, track, i;
	int success;

	if (argc < 4 || argc > 5)
		return 2;

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	status = mh_race_context_load(
		argv[1], car, track, 0, &race_context, &error);

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
		roster[i] = &race_context.scene.assets.opponents[i];

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, roster, 7,
			&race_context.scene.assets.ai_path, 1, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_load(argv[1], &race_context.hud, &error);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load(argv[1], 8, "Player",
			&race_context.hud.atlas, &race_context.results, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_names_prepare(argv[1],
			&race_context.scene, &field,
			race_context.results.profiles,
			race_context.results.count, &error);

	if (status == MH_ERROR_NONE) {
		mh_field_matrices(&field, 1.5, models, matrix, view);
		status = mh_race_scene_names(&race_context.scene, &field,
			models, view, 960, 640, 1, &error);
	}

	for (i = 0; i < 8 && status == MH_ERROR_NONE; i++)
		if (!(race_context.scene.assets.meshes[1 + 5 * i].radius > 1 &&
			race_context.scene.assets.meshes[1 + 5 * i].radius <
				5))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"root radius lost in model loading");

	if (status == MH_ERROR_NONE)
		status = check_name_policies(models, view, &error);

	success = status == MH_ERROR_NONE;

	if (success && argc == 5)
		success = capture_names(argv[4], models, view);

	/*
	 * Wide native custom nicknames cannot corrupt the atlas or original
	 * profile.
	 */
	if (success) {
		memset(race_context.results.profiles[0].nick, 'W', 15);
		race_context.results.profiles[0].nick[15] = 0;
		status = mh_race_scene_names_prepare(argv[1],
			&race_context.scene, &field,
			race_context.results.profiles,
			race_context.results.count, &error);
		success = status == MH_ERROR_NONE &&
			strlen(race_context.results.profiles[0].nick) == 15;
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);
	else if (!success)
		fprintf(stderr, "name capture: %s\n", SDL_GetError());

	mh_race_context_free(&race_context);

	if (success)
		puts("eight-driver name atlas, camera modes, availability, sphere cull and wide names passed");

	return success ? 0 : 1;
}
