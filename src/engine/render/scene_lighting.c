#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/object_light.h"
#include "engine/render/scene_internal.h"
#include "engine/render/unavailable_mesh.h"
#include "engine/simulation/camera.h"

enum mh_error_code
mh_race_scene_brightness(struct mh_race_scene *scene,
	const struct mh_weather_style *style, int dimmed,
	const struct mh_av_settings *settings, struct mh_error *error_out)
{
	struct mh_brightness brightness;
	enum mh_error_code status;
	int material_colors;

	material_colors = scene->assets.profile == MH_RENDER_DIRECT3D ||
		scene->assets.profile == MH_RENDER_POWERVR;

	status = mh_brightness_prepare(scene->assets.track.world_brightness,
		scene->assets.track.object_brightness,
		settings == NULL ? NULL : settings->brightness, dimmed, style,
		&brightness, error_out);

	if (status == MH_ERROR_NONE &&
		(!scene->visuals.brightness_ready ||
			memcmp(brightness.world,
				scene->visuals.brightness.world,
				sizeof(brightness.world)) != 0)) {
		if (material_colors)
			status = mh_mesh_material_world_brightness(
				&scene->assets.meshes[0], brightness.world,
				error_out);
		else
			status = mh_mesh_world_brightness(
				&scene->assets.meshes[0], brightness.world,
				error_out);
	}

	if (status == MH_ERROR_NONE &&
		scene->assets.profile == MH_RENDER_SOFTWARE &&
		scene->visuals.shadow_mesh.world_colors != NULL)
		status = mh_mesh_world_brightness(&scene->visuals.shadow_mesh,
			brightness.world, error_out);

	if (status == MH_ERROR_NONE) {
		scene->visuals.brightness = brightness;
		scene->visuals.brightness_ready = 1;
	}

	return status;
}

enum mh_error_code
mh_race_scene_pause(struct mh_race_scene *scene, int dimmed,
	const struct mh_av_settings *settings, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (scene == NULL || (dimmed != 0 && dimmed != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid pause lighting");

	status = mh_race_scene_brightness(
		scene, scene->visuals.weather, dimmed, settings, error_out);

	if (status == MH_ERROR_NONE)
		scene->visuals.pause_dim = dimmed;

	return status;
}

enum mh_error_code
mh_race_scene_weather(struct mh_race_scene *scene, const struct mh_field *field,
	const struct mh_av_settings *settings, struct mh_error *error_out)
{
	const struct mh_weather_style *style;
	unsigned long random;
	unsigned int flags;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid weather field");

	flags = field->cars[0].cheats;
	style = NULL;
	random = scene->visuals.particle_system.random;

	if (flags & MH_CHEAT_THUNDER) {
		status = mh_thunder_frame(&random, &style, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (flags & MH_CHEAT_UNDERWATER)
		style = mh_water_style();

	status = mh_race_scene_brightness(
		scene, style, scene->visuals.pause_dim, settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	scene->visuals.weather = style;
	scene->visuals.particle_system.random = random;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
shade_car(struct mh_race_scene *scene, const struct mh_field *field,
	unsigned int index, const float *models, const float view[16],
	int unavailable, int glenz, int reflections, struct mh_error *error_out)
{
	struct mh_point_light light;
	size_t light_capacity;
	struct mh_mesh *mesh;
	struct mh_vertex **vertices;
	const float *model, *root;
	enum mh_error_code status;
	double origin[3], root_origin[3], far_distance;
	size_t cursor;
	size_t light_count;
	unsigned int axis;
	int material_colors;

	if (index == 0 || index >= 1 + MH_CAR_MESHES * field->count ||
		index >= MH_FIELD_MESHES ||
		scene->assets.lighting_world.cells == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car lighting mesh");

	mesh = &scene->assets.meshes[index];
	vertices = &scene->visuals.shaded_vertices[index];
	model = models + index * 16;
	root = models + (1 + (index - 1) / MH_CAR_MESHES * MH_CAR_MESHES) * 16;

	for (axis = 0; axis < 3; axis++) {
		origin[axis] = model[12 + axis];
		root_origin[axis] = root[12 + axis];
	}

	status = mh_world_lights_begin(
		&scene->assets.lighting_world, root_origin, &cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	light_capacity = scene->assets.lighting_world.light_count;
	light_count = 0;

	while (mh_world_light_next(
		&scene->assets.lighting_world, &cursor, &light)) {
		if (light_count >= light_capacity) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"track light list exceeds pool");
		}

		status = mh_light_prepare(&light, origin,
			field->cars[mh_field_viewed(field)].eye,
			scene->assets.track.light_intensity,
			&scene->visuals.prepared_lights[light_count++],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (*vertices == NULL) {
		*vertices = (struct mh_vertex *)calloc(
			mesh->vertex_count, sizeof(struct mh_vertex));

		if (*vertices == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory shading car");
	}

	/* Clamp the camera far distance after percentage. */
	far_distance =
		mh_drive_camera_far(&field->cars[mh_field_viewed(field)]);

	material_colors = scene->assets.profile == MH_RENDER_DIRECT3D ||
		scene->assets.profile == MH_RENDER_POWERVR;

	if (material_colors) {
		status = mh_mesh_prepare_material_rgb(mesh, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (!unavailable && material_colors) {
		return mh_mesh_material_light(mesh,
			scene->visuals.prepared_lights, light_count,
			scene->assets.track.object_ambient,
			scene->visuals.brightness.object, model, MH_LIGHT_CAR,
			reflections, *vertices, mesh->vertex_count,
			mesh->material_rgb, error_out);
	}

	if (!unavailable)
		return (glenz ? mh_mesh_glenz_light : mh_mesh_object_light)(
			mesh, scene->visuals.prepared_lights, light_count,
			scene->assets.track.object_ambient,
			scene->visuals.brightness.object, model,
			(index - 1) % MH_CAR_MESHES == 0 ? MH_LIGHT_CAR
							: MH_LIGHT_OBJECT,
			*vertices, mesh->vertex_count, error_out);

	return mh_unavailable_mesh_update(mesh, scene->visuals.prepared_lights,
		light_count, scene->assets.track.object_ambient,
		field->cars[mh_field_viewed(field)].eye, model, view,
		material_colors ? MH_CAMERA_RACE_NEAR : 0, far_distance,
		*vertices, mesh->vertex_count,
		material_colors ? mesh->material_rgb : NULL, error_out);
}

enum mh_error_code
mh_race_scene_shade_unavailable(struct mh_race_scene *scene,
	const struct mh_field *field, unsigned int mesh, const float *models,
	const float view[16], struct mh_error *error_out)
{
	return shade_car(scene, field, mesh, models, view, 1, 0, 0, error_out);
}

enum mh_error_code
mh_race_scene_shade_car(struct mh_race_scene *scene,
	const struct mh_field *field, unsigned int mesh, const float *models,
	const float view[16], int glenz, int reflections,
	struct mh_error *error_out)
{
	return shade_car(scene, field, mesh, models, view, 0, glenz,
		reflections, error_out);
}
