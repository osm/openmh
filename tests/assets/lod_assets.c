#include <stdlib.h>
#include <string.h>

#include "../support/detail_compare.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/assets/myo.h"
#include "engine/assets/remap.h"
#include "engine/core/error.h"
#include "engine/geometry/model.h"
#include "engine/render/scene_matrices.h"
#include "engine/storage/asset_path.h"

static struct mh_race_context race_context;
static struct mh_field field;

static enum mh_error_code
read_asset(const char *root, const char *relative, struct mh_file *file,
	struct mh_error *error_out)
{
	char path[1024];
	enum mh_error_code status;

	status = mh_asset_path(root, relative, path, sizeof(path), error_out);

	return status != MH_ERROR_NONE ? status
				: mh_file_read(path, file, error_out);
}

static enum mh_error_code
prepare_capture_materials(const char *root, unsigned int track,
	struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_atlas_map remap;
	struct mh_texture_groups groups;
	char relative[64];
	enum mh_error_code status;

	memset(&remap, 0, sizeof(remap));

	/*
	 * Match the packed mesh's atlas sampling and registration order.
	 * Geometry and standalone image bytes were compared independently.
	 */
	sprintf(relative, "Tracks/track%u/Packed/remap%u.bin", track,
		race_context.scene.assets.texture_quality);
	status = read_asset(root, relative, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_atlas_remap_decode(file.data, file.size, &remap, error_out);
	groups = race_context.scene.assets.texture_groups;

	if (status == MH_ERROR_NONE)
		status = mh_mesh_register_textures(
			mesh, &remap, &groups, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mesh_bind_atlas(mesh, &remap,
			&race_context.scene.assets.texture_atlas, error_out);

	free(file.data);
	mh_atlas_map_free(&remap);

	return status;
}

static unsigned int
expected_detail(enum mh_render_profile profile, unsigned int mode,
	unsigned int distance_case)
{
	if (mode == 0) {
		if (profile == MH_RENDER_SOFTWARE)
			return distance_case;

		return distance_case >= 2 ? 1 : 0;
	}

	if (mode == 1)
		return 2;

	if (mode == 2)
		return 3;

	return 0;
}

static enum mh_error_code
check_detail_policies(float *models, enum mh_render_profile profile,
	struct mh_error *error_out)
{
	const float hardware_distances[4] = {0, 37.5f, 37.500004f, 500};
	const float software_distances[4] = {37.5f, 75, 112.5f, 500};
	const float *distances;
	const struct mh_mesh *body;
	unsigned int i, j, mode, focal, expected;
	enum mh_error_code status;

	distances = profile == MH_RENDER_SOFTWARE ? software_distances
						: hardware_distances;

	body = &race_context.scene.assets.meshes[1];

	memset(field.cars[0].camera.position, 0,
		sizeof(field.cars[0].camera.position));

	for (mode = 0; mode < 4; mode++)
		for (focal = 1; focal < 4; focal++)
			for (i = 0; i < 4; i++) {
				field.cars[0].cheats = mode == 1 || mode == 3
					? MH_CHEAT_IGNITION
					: mode == 2 ? MH_CHEAT_SUPERCARS
						: 0;
				field.unavailable_mask = mode == 3 ? 1 : 0;
				field.cars[0].camera_mode = focal == 2
					? MH_CAMERA_TRACK
					: MH_CAMERA_CHASE;
				field.cars[0].camera_goal = focal == 3;
				field.cars[0].camera_focal = focal == 1 ? 1 : 2;
				models[28] = models[29] = 0;
				models[30] = distances[i] *
					field.cars[0].camera_focal;
				status = mh_race_scene_details(
					&race_context.scene, &field, models,
					960, profile, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				expected = expected_detail(profile, mode, i);

				if (expected >= body->lod_count)
					expected = body->lod_count - 1;

				if (race_context.scene.visuals.coarse[0] !=
					(profile == MH_RENDER_SOFTWARE &&
						mode == 0 && i == 3 &&
						body->lod_count >= 3))
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"runtime detail lighting flag differs");

				if (body->lod != expected ||
					!mh_race_scene_part_visible(
						&race_context.scene, &field, 1))
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"runtime detail decision differs");

				for (j = 2; j <= 5; j++)
					if (mh_race_scene_part_visible(
						&race_context.scene, &field,
						j) !=
						(race_context.scene.assets
									.body_start_detail !=
								2 &&
							expected <= 1))
						return mh_error_set(error_out,
							MH_ERROR_FAILED,
							"runtime detail retained wrong wheel children");
			}

	field.active_mask = 0;

	for (j = 1; j <= 5; j++)
		if (mh_race_scene_part_visible(&race_context.scene, &field, j))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"inactive part remains visible");

	field.active_mask = 1;
	field.unavailable_mask = 0;
	field.cars[0].cheats = 0;
	field.cars[0].camera_goal = 0;
	field.cars[0].camera_mode = MH_CAMERA_IN_CAR;

	for (j = 1; j <= 5; j++)
		if (mh_race_scene_part_visible(&race_context.scene, &field, j))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"cockpit part remains visible");

	field.cars[0].camera_mode = MH_CAMERA_CHASE;
	field.cars[0].camera_focal = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
capture_detail(struct mh_renderer *renderer, const struct mh_mesh *body,
	unsigned int level, const char *prefix, const char *kind,
	const float *models, const float *matrix, const float *view)
{
	char path[1024];
	unsigned int i, effect;

	if (strlen(prefix) > 900)
		return 0;

	for (i = 1; i < 5; i++)
		if (!mh_renderer_mesh_visible(renderer, i,
			race_context.scene.assets.body_start_detail != 2 &&
				level <= 1))
			return 0;

	for (effect = 0; effect < 2; effect++) {
		if (!mh_renderer_mesh_unavailable(
			renderer, 0, effect ? body->vertices : NULL))
			return 0;

		sprintf(path, "%s-%s-%u-%u.bmp", prefix, kind, level, effect);

		if (!mh_renderer_scene_frame(
			renderer, matrix, view, models + 16, 1) ||
			!mh_renderer_capture(renderer, path))
			return 0;
	}

	return 1;
}

static enum mh_error_code
check_opponent_models(const char *root, unsigned int car, unsigned int track,
	const char *sources, const struct mh_image_collection *archive,
	const struct mh_image_collection *secondary,
	const struct mh_file *environment, struct mh_error *error_out)
{
	struct mh_file bytes;
	struct mh_model model;
	struct mh_mesh single, *body;
	char relative[1024];
	enum mh_error_code status;
	unsigned int entry, level;

	status = mh_race_scene_add_opponents(
		root, car, track, 7, &race_context.scene, error_out);

	for (entry = 0; entry < 7 && status == MH_ERROR_NONE; entry++) {
		body = &race_context.scene.assets.meshes[6 + 5 * entry];

		if (body->lod_count != 4)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"opponent detail chain incomplete");

		for (level = 0; level < 4 && status == MH_ERROR_NONE; level++) {
			body->lod = level;
			memset(&single, 0, sizeof(single));
			sprintf(relative, "%s%c.myo",
				race_context.scene.assets.opponents[entry].body,
				sources[level]);
			status = read_asset(root, relative, &bytes, error_out);

			if (status != MH_ERROR_NONE)
				break;

			memset(&model, 0, sizeof(model));

			status = mh_model_myo_decode(
				bytes.data, bytes.size, &model, error_out);

			if (status == MH_ERROR_NONE && level == 0 &&
				body->radius != (float)model.radius)
				status =
					mh_error_set(error_out, MH_ERROR_FAILED,
						"opponent root sphere differs");

			if (status == MH_ERROR_NONE)
				status = mh_mesh_build_native(
					mh_image_fallback_decoder(), &model,
					archive, &single, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_mesh_prepare_track_textures(
					mh_image_fallback_decoder(), &single,
					archive, secondary,
					&race_context.scene.assets.track,
					MH_TEXTURE_PALETTE_RGB,
					MH_TEXTURE_ARGB1555, error_out);

			free(bytes.data);
			mh_model_free(&model);

			if (status == MH_ERROR_NONE)
				status = mh_mesh_set_environment(
					mh_image_fallback_decoder(), &single,
					environment->data, environment->size,
					MH_TEXTURE_PALETTE_RGB, error_out);

			if (status == MH_ERROR_NONE) {
				mh_mesh_track_materials(&single,
					&race_context.scene.assets.track);
				status = compare(body, &single, error_out);
			}

			mh_mesh_free(&single);
		}
	}

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_file bytes, archive_bytes, secondary_bytes, environment;
	struct mh_image_collection *archive = NULL, *secondary = NULL;
	struct mh_model model;
	struct mh_mesh single;
	struct mh_drive drive;
	struct mh_av_settings settings;
	struct mh_renderer *packed_renderer, *single_renderer;
	const struct mh_mesh *meshes[5];
	struct mh_error error;
	enum mh_error_code status;
	char relative[1024];
	float models[MH_FIELD_MESHES * 16], matrix[16], view[16];
	unsigned int car, track, level, levels, i, quality, source;
	const char *sources, *prefix;
	int capture_frames;

	if (argc < 4 || argc > 8)
		return 2;

	car = (unsigned int)atoi(argv[2]);
	track = (unsigned int)atoi(argv[3]);

	if (car < 1 || car > 10 || track < 1 || track > 8)
		return 2;

	quality = 2;
	sources = "0123";
	prefix = argc == 5 ? argv[4] : NULL;

	if (argc >= 7) {
		if (strcmp(argv[4], "--quality") != 0 || strlen(argv[5]) != 1 ||
			argv[5][0] < '0' || argv[5][0] > '2' ||
			strlen(argv[6]) != 4)
			return 2;

		quality = (unsigned int)(argv[5][0] - '0');
		sources = argv[6];

		for (i = 0; i < 4; i++)
			if (sources[i] < '0' || sources[i] > '3')
				return 2;

		prefix = argc == 8 ? argv[7] : NULL;
	}

	levels = argc == 6 ? (unsigned int)atoi(argv[5]) : 4;

	if (levels < 1 || levels > 4 ||
		(argc == 6 && strcmp(argv[4], "--levels") != 0))
		return 2;

	capture_frames = prefix != NULL;
	memset(&archive_bytes, 0, sizeof(archive_bytes));
	memset(&environment, 0, sizeof(environment));
	memset(&secondary_bytes, 0, sizeof(secondary_bytes));
	memset(&single, 0, sizeof(single));
	packed_renderer = NULL;
	status = mh_av_settings_init(&settings, 0, 0, &error);
	settings.car_detail = quality;

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_settings(argv[1], car, track, 0,
			&settings, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, &error);

	if (status == MH_ERROR_NONE) {
		mh_field_matrices(&field, 1.5, models, matrix, view);
		status = check_detail_policies(models, MH_RENDER_GLIDE, &error);

		if (status == MH_ERROR_NONE)
			status = check_detail_policies(
				models, MH_RENDER_SOFTWARE, &error);

		field.cars[0] = drive;
		mh_field_matrices(&field, 1.5, models, matrix, view);
	}

	sprintf(relative, "Tracks/track%u/Packed/tex1.pdi", track);

	if (status == MH_ERROR_NONE)
		status = read_asset(argv[1], relative, &archive_bytes, &error);

	if (status == MH_ERROR_NONE)
		status = mh_image_pdi_open(archive_bytes.data,
			archive_bytes.size, &archive, &error);

	sprintf(relative, "Tracks/track%u/Packed/tex2.pdi", track);

	if (status == MH_ERROR_NONE)
		status =
			read_asset(argv[1], relative, &secondary_bytes, &error);

	if (status == MH_ERROR_NONE)
		status = mh_image_pdi_open(secondary_bytes.data,
			secondary_bytes.size, &secondary, &error);

	if (status == MH_ERROR_NONE)
		status = read_asset(argv[1],
			race_context.scene.assets.track.environment[0],
			&environment, &error);

	for (i = 0; i < 5; i++)
		meshes[i] = &race_context.scene.assets.meshes[1 + i];

	if (status == MH_ERROR_NONE && capture_frames) {
		SDL_SetHintWithPriority(
			SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

		if (!SDL_Init(SDL_INIT_VIDEO) ||
			(packed_renderer = mh_sdl_renderer_create_scene(
				NULL, meshes, 5)) == NULL)
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
	}

	if (status == MH_ERROR_NONE &&
		race_context.scene.assets.meshes[1].lod_count != levels)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"unexpected model detail count");

	for (level = 0; level < levels && status == MH_ERROR_NONE; level++) {
		race_context.scene.assets.meshes[1].lod = level;
		source = (unsigned int)(sources[level] - '0');
		sprintf(relative, "%s%u.myo",
			race_context.scene.assets.car.body, source);
		status = read_asset(argv[1], relative, &bytes, &error);

		if (status != MH_ERROR_NONE)
			break;

		memset(&model, 0, sizeof(model));

		status = mh_model_myo_decode(
			bytes.data, bytes.size, &model, &error);

		if (status == MH_ERROR_NONE && level == 0 &&
			race_context.scene.assets.meshes[1].radius !=
				(float)model.radius)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"quality did not preserve the selected root sphere");

		if (status == MH_ERROR_NONE)
			status = mh_mesh_build_native(
				mh_image_fallback_decoder(), &model, archive,
				&single, &error);

		if (status == MH_ERROR_NONE)
			status = mh_mesh_prepare_track_textures(
				mh_image_fallback_decoder(), &single, archive,
				secondary, &race_context.scene.assets.track,
				MH_TEXTURE_PALETTE_RGB, MH_TEXTURE_ARGB1555,
				&error);

		free(bytes.data);
		mh_model_free(&model);

		if (status == MH_ERROR_NONE)
			status = mh_mesh_set_environment(
				mh_image_fallback_decoder(), &single,
				environment.data, environment.size,
				MH_TEXTURE_PALETTE_RGB, &error);

		if (status == MH_ERROR_NONE) {
			mh_mesh_track_materials(
				&single, &race_context.scene.assets.track);
			status = compare(&race_context.scene.assets.meshes[1],
				&single, &error);
		}

		if (status == MH_ERROR_NONE && packed_renderer != NULL)
			status = prepare_capture_materials(
				argv[1], track, &single, &error);

		if (status == MH_ERROR_NONE && packed_renderer != NULL) {
			meshes[0] = &single;
			single_renderer =
				mh_sdl_renderer_create_scene(NULL, meshes, 5);

			if (single_renderer == NULL ||
				!capture_detail(packed_renderer,
					&race_context.scene.assets.meshes[1],
					level, prefix, "packed", models, matrix,
					view) ||
				!capture_detail(single_renderer, &single, level,
					prefix, "single", models, matrix, view))
				status = mh_error_set(&error, MH_ERROR_BACKEND,
					SDL_GetError());

			mh_renderer_destroy(single_renderer);
		}

		mh_mesh_free(&single);
	}

	if (status == MH_ERROR_NONE && packed_renderer != NULL) {
		race_context.scene.assets.meshes[1].lod = 0;

		if (!capture_detail(packed_renderer,
			&race_context.scene.assets.meshes[1], 0, prefix,
			"restored", models, matrix, view))
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
	}

	if (status == MH_ERROR_NONE && argc == 8)
		status = check_opponent_models(argv[1], car, track, sources,
			archive, secondary, &environment, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);
	else
		printf("car%u track%u: %u detail models, materials, primitives and halos match\n",
			car, track, levels);
	mh_renderer_destroy(packed_renderer);

	if (capture_frames)
		SDL_Quit();

	mh_image_collection_close(archive);
	free(archive_bytes.data);
	mh_image_collection_close(secondary);
	free(secondary_bytes.data);
	free(environment.data);
	mh_mesh_free(&single);
	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
