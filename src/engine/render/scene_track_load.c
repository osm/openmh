#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/mesh_load.h"
#include "engine/assets/path_tracks.h"
#include "engine/catalog/load.h"
#include "engine/collision/load.h"
#include "engine/core/error.h"
#include "engine/render/object_light.h"
#include "engine/render/scene_internal.h"
#include "interfaces/animation/source.h"
#include "interfaces/material/source.h"
#include "interfaces/navigation/source.h"

enum mh_error_code
mh_race_scene_load_track_config(const struct mh_resource_source *resources,
	const char *root, unsigned int track, int *scenery,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_track_config_load_from(scene->catalog_source, resources,
		root, track, &scene->assets.track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	scene->assets.track_number = track;

	if (scene->assets.track.scene_file[0] == 0)
		*scenery = 0;

	scene->assets.scenery = *scenery != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_load_track_data(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	char relative[1024];
	unsigned int camera;
	enum mh_error_code status;

	if (scene->assets.scenery) {
		status = mh_object_assets_load_from(scene->animation_source,
			scene->model_decoder, scene->collision_source,
			resources, root, &scene->assets.track,
			&scene->assets.objects, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (scene->assets.objects.count > MH_FIELD_OBJECTS) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many scenery physics bodies");
		}
	}

	status = mh_collision_load_from(scene->collision_source, resources,
		root, scene->assets.track.collision_file,
		&scene->assets.world_collision, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_route_load_from(scene->navigation_source, resources, root,
		scene->assets.track.spline_file, &scene->assets.route,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scene->assets.track.camera_file[0] != 0) {
		status = mh_camera_motion_load_from(scene->navigation_source,
			resources, root, scene->assets.track.camera_file,
			&scene->assets.camera_motion, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (camera = 0; camera < 2; camera++)
		if (scene->assets.track.start_camera[camera][0] != 0) {
			sprintf(relative, "%s/%s",
				scene->assets.track.base_path,
				scene->assets.track.start_camera[camera]);
			status = mh_motion_load_from(scene->animation_source,
				resources, root, relative,
				&scene->assets.start_camera[camera], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	return mh_materials_load_from(scene->material_source, resources, root,
		scene->assets.track.materials_file, &scene->assets.materials,
		error_out);
}

enum mh_error_code
mh_race_scene_load_world(const struct mh_resource_source *resources,
	const char *root, int scenery,
	const struct mh_image_collection *primary, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	size_t light_capacity;
	enum mh_error_code status;

	status = mh_world_source_load_from(scene->world_decoder, resources,
		root, &scene->assets.track,
		scene->assets.profile == MH_RENDER_SOFTWARE,
		&scene->assets.lighting_world, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_world_mesh_build_from(scene->animation_source,
		scene->model_decoder, scene->image_decoder, resources, root,
		&scene->assets.track, &scene->assets.lighting_world, scenery, 1,
		primary, scene->assets.profile, &scene->assets.meshes[0],
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	light_capacity = scene->assets.lighting_world.light_count;

	if (light_capacity != 0) {
		scene->visuals.prepared_lights =
			(struct mh_prepared_light *)calloc(
				(size_t)light_capacity,
				sizeof(*scene->visuals.prepared_lights));

		if (scene->visuals.prepared_lights == NULL) {
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory preparing track lights");
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_load_ai_from(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	char relative[512];
	size_t length;

	if (scene->assets.ai_path.points != NULL ||
		scene->assets.track.ai_path[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing or already loaded AI path");

	length = strlen(scene->assets.track.ai_path);
	sprintf(relative, MH_ASSET_TRACK_AI_FORMAT, scene->assets.track.ai_path,
		scene->assets.track.ai_path[length - 1] == '\\' ||
				scene->assets.track.ai_path[length - 1] == '/'
			? ""
			: "\\");

	return mh_ai_path_load_from(scene->navigation_source, resources, root,
		relative, &scene->assets.ai_path, error_out);
}
