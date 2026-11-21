#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/object_assets.h"
#include "engine/assets/path_tracks.h"
#include "engine/collision/data.h"
#include "engine/collision/load.h"
#include "engine/core/error.h"
#include "engine/geometry/load.h"
#include "engine/geometry/model.h"
#include "engine/storage/asset_path.h"
#include "interfaces/animation/source.h"
#include "interfaces/storage/resource.h"

#define HULL_BANK_LIMIT (64UL * 1024 * 1024)

void
mh_object_assets_free(struct mh_object_assets *assets)
{
	size_t i;

	if (assets == NULL)
		return;

	for (i = 0; i < assets->model_count; i++)
		mh_collision_free(&assets->models[i].hull);

	free(assets->models);
	free(assets->objects);
	mh_scene_free(&assets->scene);
	memset(assets, 0, sizeof(*assets));
}

static enum mh_error_code
load_model(const struct mh_model_decoder *model_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_resource_source *source, const char *root,
	const char *base_path, const char *name, size_t *hull_bytes,
	struct mh_object_model *model, struct mh_error *error_out)
{
	struct mh_model geometry;
	struct mh_file file;
	char relative[768];
	size_t length;
	enum mh_error_code status;

	strcpy(model->name, name);
	sprintf(relative, MH_ASSET_TRACK_OBJECT_FORMAT, base_path, name);
	status = mh_model_load_from(
		model_decoder, source, root, relative, &geometry, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(model->minimum, geometry.minimum,
			sizeof(model->minimum));
		memcpy(model->maximum, geometry.maximum,
			sizeof(model->maximum));

		if (geometry.vertex_count == 0)
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"empty scenery physics model");

		mh_model_free(&geometry);
	}

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(relative);
	strcpy(relative + length - 4, ".col");
	memset(&file, 0, sizeof(file));
	status = mh_collision_read_from(
		collision_source, source, root, relative, &file, error_out);

	if (status == MH_ERROR_NONE &&
		file.size > HULL_BANK_LIMIT - *hull_bytes)
		status = mh_error_set(error_out, MH_ERROR_RANGE,
			"scenery collision bank exceeds 64 MiB");

	if (status == MH_ERROR_NONE)
		status = mh_collision_source_decode(collision_source, file.data,
			file.size, &model->hull, error_out);

	if (status == MH_ERROR_NONE)
		*hull_bytes += file.size;

	free(file.data);

	return status;
}

enum mh_error_code
mh_object_assets_load_from(const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, struct mh_object_assets *output,
	struct mh_error *error_out)
{
	struct mh_object_assets assets;
	struct mh_object_asset *object;
	struct mh_object_model *model;
	struct mh_scene_object *scene_object;
	size_t scene_index, model_index, hull_bytes;
	enum mh_error_code status;

	if (root == NULL || track == NULL || output == NULL ||
		track->scene_file[0] == 0 || track->base_path[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery physics asset request");

	memset(&assets, 0, sizeof(assets));
	hull_bytes = 0;
	status = mh_scene_load_from(animation_source, source, root,
		track->scene_file, &assets.scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	assets.objects = (struct mh_object_asset *)calloc(
		assets.scene.object_count, sizeof(*assets.objects));
	assets.models = (struct mh_object_model *)calloc(
		assets.scene.object_count, sizeof(*assets.models));

	if (assets.objects == NULL || assets.models == NULL)
		status = mh_error_set(error_out, MH_ERROR_MEMORY,
			"scenery physics allocation failed");

	for (scene_index = 0; status == MH_ERROR_NONE &&
		scene_index < assets.scene.object_count;
		scene_index++) {
		scene_object = &assets.scene.objects[scene_index];

		if (!mh_object_has_body(scene_object))
			continue;

		for (model_index = 0; model_index < assets.model_count;
			model_index++)
			if (strcmp(assets.models[model_index].name,
				scene_object->model) == 0)
				break;

		model = &assets.models[model_index];

		if (model_index == assets.model_count) {
			assets.model_count++;
			status = load_model(model_decoder, collision_source,
				source, root, track->base_path,
				scene_object->model, &hull_bytes, model,
				error_out);

			if (status != MH_ERROR_NONE)
				break;
		}

		object = &assets.objects[assets.count];
		object->scene_index = scene_index;
		object->model_index = model_index;
		status = mh_object_body_bounds(model->minimum, model->maximum,
			scene_object->polygon_size,
			scene_object->collision_sound_class, &object->body,
			error_out);

		if (status == MH_ERROR_NONE)
			assets.count++;
	}

	if (status == MH_ERROR_NONE)
		*output = assets;
	else
		mh_object_assets_free(&assets);

	return status;
}
