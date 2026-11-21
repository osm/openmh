#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/car_paint.h"
#include "engine/assets/path_tracks.h"
#include "engine/catalog/load.h"
#include "engine/collision/load.h"
#include "engine/core/error.h"
#include "engine/geometry/load.h"
#include "engine/geometry/model.h"
#include "engine/image/load.h"
#include "engine/render/scene_internal.h"
#include "engine/render/scene_matrices.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_race_scene_load_car_part(const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *stem, int body, unsigned int minimum_detail,
	const struct mh_image_collection *primary,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_model models[MH_MESH_LODS];
	char relative[1024], path[1024];
	enum mh_error_code status;
	unsigned int variant, count, source_variant;
	size_t length;
	int found, missing;

	length = strlen(stem);

	if (length > sizeof(relative) - 6)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "car model path too long");

	memset(models, 0, sizeof(models));
	count = 0;
	missing = 0;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (variant = 0; variant < MH_MESH_LODS; variant++) {
		source_variant = body && variant < minimum_detail
			? minimum_detail
			: variant;
		memcpy(relative, stem, length);
		relative[length] = (char)('0' + source_variant);
		memcpy(relative + length + 1, MH_ASSET_MODEL_EXTENSION, 5);
		status = mh_model_path_find_from(model_decoder, resources, root,
			relative, path, sizeof(path), &found, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (!found) {
			if (!body) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"missing wheel animation variant");
				break;
			}

			if (variant == 0) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"missing base car model");
				break;
			}

			missing = 1;
			continue;
		}

		if (missing) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"non-contiguous car detail chain");
			break;
		}

		status = mh_resource_source_read(
			resources, path, &file, error_out);

		if (status == MH_ERROR_NONE) {
			status = mh_model_decode(model_decoder, file.data,
				file.size, &models[variant], error_out);
			free(file.data);
		}

		if (status != MH_ERROR_NONE)
			break;

		count++;
	}

	if (status == MH_ERROR_NONE)
		status = mh_mesh_build_lods_profile(decoder, models, count,
			primary, profile, mesh, error_out);

	for (variant = 0; variant < MH_MESH_LODS; variant++)
		mh_model_free(&models[variant]);

	return status;
}

static enum mh_error_code
load_hull(const struct mh_resource_source *resources, const char *root,
	const struct mh_car *car, unsigned int index,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	if (car->collision_file[0] == 0 ||
		scene->assets.car_hulls[index].cells != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing or already loaded car hull");

	return mh_collision_load_from(scene->collision_source, resources, root,
		car->collision_file, &scene->assets.car_hulls[index],
		error_out);
}

enum mh_error_code
mh_race_scene_load_car(const struct mh_resource_source *resources,
	const char *root, unsigned int number, unsigned int entry,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	struct mh_car *car;
	enum mh_error_code status;

	car = entry == 0 ? &scene->assets.car
			: &scene->assets.opponents[entry - 1];
	status = mh_car_config_load_from(
		scene->catalog_source, resources, root, number, car, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_hull(resources, root, car, entry, scene, error_out);

	if (status == MH_ERROR_NONE && MH_RENDER_BUILD_SOFTWARE &&
		scene->assets.profile == MH_RENDER_SOFTWARE)
		status = mh_software_halo_assets_load_from(scene->image_decoder,
			resources, root, &car->halos,
			&scene->assets.software.car_halos[entry], error_out);

	return status;
}

enum mh_error_code
mh_race_scene_paint(struct mh_race_scene *scene, unsigned int entry,
	const unsigned long colors[3], struct mh_error *error_out)
{
	const struct mh_car *car;

	if (scene == NULL || entry >= MH_RACE_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid paint entrant");

	car = entry == 0 ? &scene->assets.car
			: &scene->assets.opponents[entry - 1];

	if (!car->has_paint_keys ||
		scene->assets.meshes[1 + entry * MH_CAR_MESHES].face_count == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing car paint keys or model");

	return mh_car_paint_apply(
		&scene->assets.meshes[1 + entry * MH_CAR_MESHES],
		car->paint_keys, colors, error_out);
}

enum mh_error_code
mh_race_scene_add_opponents_from(const struct mh_resource_source *resources,
	const char *root, unsigned int car, unsigned int track,
	unsigned int count, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	unsigned int choices[MH_TRAFFIC_MAX_CARS - 1], i, car_count;
	enum mh_error_code status;

	if (count > MH_TRAFFIC_MAX_CARS - 1 || car < 1 ||
		car > MH_CAR_CATALOG_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid opponent roster");

	status = mh_car_catalog_count_from(
		scene->catalog_source, resources, root, &car_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (car > car_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"selected car is absent from catalog");

	for (i = 0; i < count; i++)
		choices[i] = (car + i) % car_count + 1;

	return mh_race_scene_add_selected_from(
		resources, root, choices, track, count, scene, error_out);
}

enum mh_error_code
mh_race_scene_add_selected_from(const struct mh_resource_source *resources,
	const char *root, const unsigned int *cars, unsigned int track,
	unsigned int count, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_file environment;
	struct mh_image_collection *primary, *secondary;
	struct mh_car *car;
	struct mh_mesh *mesh;
	char relative[1024];
	unsigned int opponent, part, mesh_index;
	enum mh_error_code status;

	if (count > MH_RACE_MAX_CARS - 1 || (count != 0 && cars == NULL) ||
		scene == NULL || track < 1 || track > MH_TRACK_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid selected car roster");

	for (opponent = 0; opponent < count; opponent++)
		if (cars[opponent] < 1 || cars[opponent] > MH_CAR_CATALOG_LIMIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid selected car");

	primary = secondary = NULL;
	memset(&environment, 0, sizeof(environment));
	sprintf(relative, MH_ASSET_TRACK_TEXTURES_PRIMARY_FORMAT,
		scene->assets.track.base_path);
	status = mh_image_collection_open(scene->image_source, resources, root,
		relative, &primary, error_out);

	if (scene->assets.profile != MH_RENDER_SOFTWARE) {

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_open_opaque_textures(
				resources, root, scene, &secondary, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_image_read_from(scene->image_decoder,
				resources, root,
				scene->assets.track.environment[0],
				&environment, error_out);
	}

	for (opponent = 0; opponent < count && status == MH_ERROR_NONE;
		opponent++) {
		car = &scene->assets.opponents[opponent];
		status = mh_race_scene_load_car(resources, root, cars[opponent],
			opponent + 1, scene, error_out);

		for (part = 0; part < MH_CAR_MESHES && status == MH_ERROR_NONE;
			part++) {
			mesh_index = MH_DRIVE_MESHES +
				opponent * MH_CAR_MESHES + part;
			mesh = &scene->assets.meshes[mesh_index];
			status = mh_race_scene_load_car_part(
				scene->model_decoder, scene->image_decoder,
				resources, root,
				part == 0 ? car->body : car->wheels[part - 1],
				part == 0, scene->assets.body_start_detail,
				primary, scene->assets.profile, mesh,
				error_out);

			if (status == MH_ERROR_NONE)
				mh_mesh_track_materials(
					mesh, &scene->assets.track);

			if (status == MH_ERROR_NONE &&
				scene->assets.profile == MH_RENDER_SOFTWARE)
				status = mh_mesh_load_indexed_textures(
					scene->image_decoder, mesh, primary,
					error_out);

			if (status == MH_ERROR_NONE &&
				scene->assets.profile != MH_RENDER_SOFTWARE)
				status = mh_mesh_prepare_track_textures(
					scene->image_decoder, mesh, primary,
					scene->assets.profile == MH_RENDER_GLIDE
						? secondary
						: primary,
					&scene->assets.track,
					scene->assets.opaque_format,
					scene->assets.alpha_format, error_out);

			if (status == MH_ERROR_NONE &&
				scene->assets.profile != MH_RENDER_SOFTWARE)
				status = mh_mesh_set_environment(
					scene->image_decoder, mesh,
					environment.data, environment.size,
					scene->assets.opaque_format, error_out);
		}
	}

	mh_image_collection_close(primary);
	mh_image_collection_close(secondary);
	free(environment.data);

	if (status != MH_ERROR_NONE ||
		scene->assets.profile == MH_RENDER_SOFTWARE)
		return status;

	return mh_race_scene_register_textures(resources, root, NULL, NULL,
		scene, MH_DRIVE_MESHES + count * MH_CAR_MESHES, error_out);
}
