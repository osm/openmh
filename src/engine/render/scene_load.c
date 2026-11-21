#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_tracks.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#if MH_RENDER_BUILD_OPENMH
#include "engine/render/openmh_scene_lighting.h"
#endif
#include "engine/render/scene_internal.h"
#include "engine/render/scene_matrices.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_race_load_complete(const struct mh_race_load_observer *observer,
	enum mh_race_load_stage stage, struct mh_error *error_out)
{
	if (observer == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (observer->complete == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race-load callback");

	return observer->complete(observer->context, stage, error_out);
}

void
mh_race_scene_free(struct mh_race_scene *scene)
{
	unsigned int i;

	for (i = 0; i < MH_FIELD_MESHES; i++) {
		mh_mesh_free(&scene->assets.meshes[i]);
		free(scene->visuals.shaded_vertices[i]);
		free(scene->visuals.glenz.coordinates[i]);
	}

	if (MH_RENDER_BUILD_SOFTWARE)
		mh_software_scene_assets_free(&scene->assets.software);

	mh_finish_free(&scene->assets.finish);

	for (i = 0; i < MH_FINISH_SIGNS; i++)
		free(scene->visuals.finish_vertices[i]);

	free(scene->visuals.prepared_lights);
	free(scene->assets.retro);
	mh_mesh_free(&scene->visuals.skid_mesh);
	mh_mesh_free(&scene->visuals.particle_mesh);
	mh_mesh_free(&scene->visuals.sky_mesh);
	mh_lamps_free(&scene->visuals.lamps);
	free(scene->visuals.software_halos.sources);
	mh_name_mesh_free(&scene->visuals.names);
	mh_mesh_free(&scene->visuals.shadow_mesh);
	mh_world_free(&scene->assets.lighting_world);
	mh_route_free(&scene->assets.route);
	mh_camera_motion_free(&scene->assets.camera_motion);

	for (i = 0; i < 2; i++)
		free(scene->assets.start_camera[i].keys);

	mh_ai_path_free(&scene->assets.ai_path);
	mh_object_assets_free(&scene->assets.objects);
	mh_collision_free(&scene->assets.world_collision);

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		mh_collision_free(&scene->assets.car_hulls[i]);

	mh_atlas_free(&scene->assets.texture_atlas);
	mh_atlas_free(&scene->assets.lamp_atlas);
	memset(scene, 0, sizeof(*scene));
}

enum mh_error_code
mh_race_scene_load_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	return mh_race_scene_load_settings_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, image_source, image_decoder, resources, root,
		car, track, scenery, NULL, scene, error_out);
}

enum mh_error_code
mh_race_scene_load_settings_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	return mh_race_scene_load_profile_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, image_source, image_decoder, resources, root,
		car, track, scenery, settings, MH_RENDER_GLIDE, scene,
		error_out);
}

enum mh_error_code
mh_race_scene_load_profile_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, enum mh_render_profile profile,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	return mh_race_scene_load_observed_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, image_source, image_decoder, resources, root,
		car, track, scenery, settings, profile, NULL, NULL, scene,
		error_out);
}

enum mh_error_code
mh_race_scene_load_observed_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned int car, unsigned int track, int scenery,
	const struct mh_av_settings *settings, enum mh_render_profile profile,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_image_collection *primary, *secondary;
	char relative[1024];
	unsigned int i;
	enum mh_error_code status;

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race rendering profile");

	memset(scene, 0, sizeof(*scene));
	scene->image_source = image_source;
	scene->image_decoder = image_decoder;
	scene->model_decoder = model_decoder;
	scene->world_decoder = world_decoder;
	scene->atlas_source = atlas_source;
	scene->animation_source = animation_source;
	scene->navigation_source = navigation_source;
	scene->catalog_source = catalog_source;
	scene->material_source = material_source;
	scene->env_sound_source = env_sound_source;
	scene->horn_source = horn_source;
	scene->collision_source = collision_source;
	scene->assets.profile = profile;

#if MH_RENDER_BUILD_OPENMH
	if (profile == MH_RENDER_OPENMH)
		scene->light_mesh = mh_openmh_scene_lighting;

#endif
	primary = secondary = NULL;
	status = MH_ERROR_NONE;

	if (profile != MH_RENDER_SOFTWARE)
		status = mh_race_scene_texture_budget(profile, texture_budget,
			&scene->assets.texture_budget, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	scene->assets.view_width = MH_VIEW_WIDTH;
	scene->assets.view_height = MH_VIEW_HEIGHT;
	scene->assets.opaque_format = MH_TEXTURE_XRGB8888;
	scene->assets.alpha_format = MH_TEXTURE_ARGB1555;

	if (settings != NULL) {
		if (settings->width < MH_VIEW_MIN_WIDTH ||
			settings->width > MH_VIEW_MAX_WIDTH ||
			settings->height < MH_VIEW_MIN_HEIGHT ||
			settings->height > MH_VIEW_MAX_HEIGHT)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"ScreenDimension must be 320..4096 by 200..2160");

		scene->assets.view_width = (unsigned int)settings->width;
		scene->assets.view_height = (unsigned int)settings->height;

		if (settings->car_detail > 2 || settings->track_detail > 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid geometry detail options");

		if (settings->texture_precision > MH_AV_TEXTURE_32BIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid texture precision");

		if (settings->texture_precision == MH_AV_TEXTURE_16BIT)
			scene->assets.opaque_format = MH_TEXTURE_RGB565;

		if (profile == MH_RENDER_DIRECT3D &&
			settings->texture_filter == MH_AV_FILTER_TRILINEAR)
			scene->assets.texture_atlas.border =
				MH_ATLAS_MIP_BORDER;

		scene->assets.body_start_detail = 2 - settings->car_detail;

		if (!settings->track_detail)
			scenery = 0;
	}

	/* PowerSGL uses fixed precision, independent of Direct3D settings. */
	if (profile == MH_RENDER_POWERVR) {
		scene->assets.opaque_format = MH_TEXTURE_RGB555;
		scene->assets.alpha_format = MH_TEXTURE_ARGB4444;
	}

	status = mh_race_scene_load_track_config(
		resources, root, track, &scenery, scene, error_out);

	if (status != MH_ERROR_NONE)
		goto finish;

	sprintf(relative, MH_ASSET_TRACK_TEXTURES_PRIMARY_FORMAT,
		scene->assets.track.base_path);
	status = mh_image_collection_open(scene->image_source, resources, root,
		relative, &primary, error_out);

	if (status != MH_ERROR_NONE)
		goto finish;

	if (profile != MH_RENDER_SOFTWARE) {
		status = mh_race_scene_open_opaque_textures(
			resources, root, scene, &secondary, error_out);

		if (status != MH_ERROR_NONE)
			goto finish;
	}

	status = mh_race_scene_load_world(
		resources, root, scenery, primary, scene, error_out);

	/* World loaded, before collision/routes and cars. */
	if (status == MH_ERROR_NONE)
		status = mh_race_load_complete(
			observer, MH_RACE_LOAD_WORLD, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_track_data(
			resources, root, scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_effects(
			resources, root, scene, error_out);

	/* Track support/environment resources precede the car loop. */
	if (status == MH_ERROR_NONE)
		status = mh_race_load_complete(
			observer, MH_RACE_LOAD_TRACK, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_car(
			resources, root, car, 0, scene, error_out);

	if (status != MH_ERROR_NONE)
		goto finish;

	for (i = 0; i < MH_CAR_MESHES; i++) {
		status = mh_race_scene_load_car_part(scene->model_decoder,
			scene->image_decoder, resources, root,
			i == 0 ? scene->assets.car.body
			: scene->assets.car.wheels[i - 1],
			i == 0, scene->assets.body_start_detail, primary,
			profile, &scene->assets.meshes[i + 1], error_out);

		if (status == MH_ERROR_NONE)
			mh_mesh_track_materials(&scene->assets.meshes[i + 1],
				&scene->assets.track);

		if (status != MH_ERROR_NONE)
			goto finish;
	}

	if (profile != MH_RENDER_SOFTWARE) {
		for (i = 0; i < MH_DRIVE_MESHES; i++) {
			status = mh_mesh_prepare_track_textures(
				scene->image_decoder, &scene->assets.meshes[i],
				primary,
				profile == MH_RENDER_GLIDE ? secondary
							: primary,
				&scene->assets.track,
				scene->assets.opaque_format,
				scene->assets.alpha_format, error_out);

			if (status != MH_ERROR_NONE)
				goto finish;
		}

		status = mh_image_read_from(scene->image_decoder, resources,
			root, scene->assets.track.environment[0], &file,
			error_out);

		if (status != MH_ERROR_NONE)
			goto finish;

		for (i = 0; i < MH_DRIVE_MESHES && status == MH_ERROR_NONE;
			i++) {
			status = mh_mesh_set_environment(scene->image_decoder,
				&scene->assets.meshes[i], file.data, file.size,
				scene->assets.opaque_format, error_out);
		}

		free(file.data);
	} else {
		for (i = 0; i < MH_DRIVE_MESHES && status == MH_ERROR_NONE; i++)
			status = mh_mesh_load_indexed_textures(
				scene->image_decoder, &scene->assets.meshes[i],
				primary, error_out);
	}

finish:
	if (status == MH_ERROR_NONE)
		status = mh_race_scene_brightness(
			scene, NULL, 0, settings, error_out);

	if (status == MH_ERROR_NONE && profile != MH_RENDER_SOFTWARE)
		status = mh_race_scene_register_textures(resources, root,
			primary, secondary, scene, MH_DRIVE_MESHES, error_out);

	mh_image_collection_close(primary);
	mh_image_collection_close(secondary);

	if (status != MH_ERROR_NONE)
		mh_race_scene_free(scene);

	return status;
}
