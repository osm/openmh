#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/mesh_load.h"
#include "engine/assets/path_tracks.h"
#include "engine/catalog/car.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/geometry/load.h"
#include "engine/geometry/model.h"
#include "engine/image/load.h"
#include "engine/render/mesh.h"
#include "engine/storage/asset_path.h"
#include "interfaces/animation/source.h"
#include "interfaces/catalog/car.h"
#include "interfaces/catalog/track.h"
#include "interfaces/storage/resource.h"

enum {
	MESH_PATH_SIZE = 1024,
	CAR_WHEELS = 4,
	CAR_PARTS = 1 + CAR_WHEELS
};

enum mh_error_code
mh_mesh_environment_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	if (track == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing environment track");

	status = mh_image_read_from(
		decoder, source, root, track->environment[0], &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_mesh_set_environment(decoder, mesh, file.data, file.size,
		MH_TEXTURE_PALETTE_RGB, error_out);
	free(file.data);

	if (status == MH_ERROR_NONE)
		mh_mesh_track_materials(mesh, track);

	return status;
}

static enum mh_error_code
load_scenery(const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, const struct mh_world *world,
	const struct mh_image_collection *collection, int original_units,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_model *models;
	struct mh_scene_instance *instances;
	struct mh_scene scene;
	size_t i, j;
	char relative[MESH_PATH_SIZE];
	enum mh_error_code status;

	if (track->scene_file[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"track is missing SceneName");

	status = mh_scene_load_from(animation_source, source, root,
		track->scene_file, &scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	models = (struct mh_model *)calloc(scene.object_count, sizeof(*models));
	instances = (struct mh_scene_instance *)calloc(
		scene.object_count, sizeof(*instances));
	status = mh_error_set(
		error_out, MH_ERROR_MEMORY, "out of memory loading scenery");

	if (models == NULL || instances == NULL)
		goto cleanup;

	for (i = 0; i < scene.object_count; i++) {
		for (j = 0; j < i; j++) {
			if (strcmp(scene.objects[i].model,
				scene.objects[j].model) == 0)
				break;
		}

		instances[i].pose = &scene.objects[i];

		if (j < i) {
			instances[i].model = instances[j].model;
			continue;
		}

		sprintf(relative, MH_ASSET_TRACK_OBJECT_FORMAT,
			track->base_path, scene.objects[i].model);
		status = mh_model_load_from(model_decoder, source, root,
			relative, &models[i], error_out);

		if (status != MH_ERROR_NONE)
			goto cleanup;

		instances[i].model = &models[i];
	}

	if (original_units)
		status = mh_mesh_build_world_native_profile(decoder, world,
			collection, track->world_brightness, instances,
			scene.object_count, profile, mesh, error_out);
	else
		status = mh_mesh_build_world_scene(decoder, world, collection,
			track->world_brightness, instances, scene.object_count,
			mesh, error_out);

cleanup:
	if (models != NULL) {
		for (i = 0; i < scene.object_count; i++)
			mh_model_free(&models[i]);
	}

	free(models);
	free(instances);
	mh_scene_free(&scene);

	return status;
}

enum mh_error_code
mh_world_source_load_from(const struct mh_world_decoder *world_decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, int standard_world,
	struct mh_world *world, struct mh_error *error_out)
{
	char relative[MESH_PATH_SIZE];
	enum mh_error_code status;

	if (track->base_path[0] == 0 || track->world_file[0] == 0) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"track is missing BasePath or FileName");
	}

	sprintf(relative, "%s/%s%s", track->base_path,
		standard_world ? "" : MH_ASSET_TRACK_HARDWARE_PREFIX,
		track->world_file);

	status = mh_world_load_from(
		world_decoder, source, root, relative, world, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	/* Single-renderer demos ship only one of the two visual worlds. */
	sprintf(relative, "%s/%s%s", track->base_path,
		standard_world ? MH_ASSET_TRACK_HARDWARE_PREFIX : "",
		track->world_file);

	return mh_world_load_from(
		world_decoder, source, root, relative, world, error_out);
}

enum mh_error_code
mh_world_mesh_build_from(const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, const struct mh_world *world, int scenery,
	int original_units, const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (track == NULL || world == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing track world");

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT ||
		(profile == MH_RENDER_SOFTWARE && !original_units))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world mesh profile or coordinates");

	if (scenery)
		status = load_scenery(animation_source, model_decoder, decoder,
			source, root, track, world, collection, original_units,
			profile, mesh, error_out);
	else if (original_units)
		status = mh_mesh_build_world_native_profile(decoder, world,
			collection, track->world_brightness, NULL, 0, profile,
			mesh, error_out);
	else
		status = mh_mesh_build_world(decoder, world, collection,
			track->world_brightness, mesh, error_out);

	if (status == MH_ERROR_NONE)
		mh_mesh_track_materials(mesh, track);

	return status;
}

enum mh_error_code
mh_car_mesh_load_from(const struct mh_catalog_source *catalog_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	unsigned int number, const struct mh_image_collection *collection,
	struct mh_mesh *mesh, char name[128], struct mh_error *error_out)
{
	struct mh_model models[CAR_PARTS];
	struct mh_mesh_part parts[CAR_PARTS];
	struct mh_car car;
	double wheel_offsets[CAR_WHEELS][3];
	char relative[MH_CAR_PATH_SIZE + 8];
	const char *prefix;
	unsigned int i, axis;
	enum mh_error_code status;

	status = mh_car_config_load_from(
		catalog_source, source, root, number, &car, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(name, car.name);
	mh_car_reset_offsets(&car, wheel_offsets);
	memset(models, 0, sizeof(models));

	for (i = 0; i < CAR_PARTS; i++) {
		prefix = i == 0 ? car.body : car.wheels[i - 1];
		strcpy(relative, prefix);

		/* Body detail zero and the static wheel variant zero. */
		strcat(relative, "0.myo");
		status = mh_model_load_from(model_decoder, source, root,
			relative, &models[i], error_out);

		if (status != MH_ERROR_NONE)
			break;

		parts[i].model = &models[i];

		for (axis = 0; axis < 3; axis++) {
			parts[i].offset[axis] =
				i == 0 ? 0.0 : wheel_offsets[i - 1][axis];
		}
	}

	if (status == MH_ERROR_NONE)
		status = mh_mesh_build_parts(
			decoder, parts, CAR_PARTS, collection, mesh, error_out);

	for (i = 0; i < CAR_PARTS; i++)
		mh_model_free(&models[i]);

	return status;
}
