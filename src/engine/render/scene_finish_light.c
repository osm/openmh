#include <stdlib.h>

#include "engine/render/object_light.h"
#include "engine/render/scene.h"
#include "interfaces/render/renderer.h"

enum mh_error_code
mh_race_scene_shade_finish(struct mh_race_scene *scene,
	const struct mh_field *field, const struct mh_finish_frame *frame,
	int reflections, struct mh_error *error_out)
{
	struct mh_point_light light;
	size_t light_capacity;
	struct mh_mesh *mesh;
	struct mh_vertex **vertices;
	double origin[3], brightness[3], ambient[3];
	size_t cursor;
	size_t light_count;
	unsigned int i;
	enum mh_error_code status;
	int material;

	if (frame->rank == 0 || frame->rank > MH_FINISH_SIGNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish sign lighting rank");

	mesh = &scene->assets.finish.meshes[frame->rank - 1];
	vertices = &scene->visuals.finish_vertices[frame->rank - 1];

	for (i = 0; i < 3; i++) {
		origin[i] = frame->model[12 + i];
		brightness[i] = scene->visuals.brightness.object[i];
		ambient[i] =
			scene->assets.track.object_ambient[i] * frame->color[i];
	}

	status = mh_world_lights_begin(
		&scene->assets.lighting_world, origin, &cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	light_capacity = scene->assets.lighting_world.light_count;
	light_count = 0;

	while (mh_world_light_next(
		&scene->assets.lighting_world, &cursor, &light)) {
		if (light_count >= light_capacity)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"finish sign light list exceeds pool");

		status = mh_light_prepare(&light, origin,
			field->cars[mh_field_viewed(field)].eye,
			scene->assets.track.light_intensity,
			&scene->visuals.prepared_lights[light_count++],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (*vertices == NULL) {
		*vertices = calloc(mesh->vertex_count, sizeof(**vertices));

		if (*vertices == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory shading finish sign");
	}

	material = scene->assets.profile == MH_RENDER_DIRECT3D ||
		scene->assets.profile == MH_RENDER_POWERVR;

	if (!material)
		return mh_mesh_object_light(mesh,
			scene->visuals.prepared_lights, light_count, ambient,
			brightness, frame->model, MH_LIGHT_OBJECT, *vertices,
			mesh->vertex_count, error_out);

	status = mh_mesh_prepare_material_rgb(mesh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_mesh_material_light(mesh, scene->visuals.prepared_lights,
		light_count, ambient, brightness, frame->model, MH_LIGHT_OBJECT,
		reflections, *vertices, mesh->vertex_count, mesh->material_rgb,
		error_out);
}

enum mh_error_code
mh_race_scene_finish_lighting(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_renderer *renderer,
	unsigned int finish_mesh, const struct mh_finish_frame *frame,
	int reflections, struct mh_error *error_out)
{
	struct mh_model_lighting light;
	unsigned int mesh, i;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || frame == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS ||
		frame->rank == 0 || frame->rank > MH_FINISH_SIGNS ||
		finish_mesh > (unsigned int)-1 - MH_FINISH_SIGNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish sign lighting");

	mesh = finish_mesh + frame->rank - 1;

	if (MH_RENDER_BUILD_SOFTWARE &&
		scene->assets.profile == MH_RENDER_SOFTWARE) {
		status = mh_race_scene_software_lighting(
			scene, field, 0, NULL, &light, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < 3; i++) {
			light.ambient[i] *= frame->color[i];
			light.directional_color[i] *= frame->color[i];
		}

		return mh_renderer_world_lighting(renderer, mesh, mesh,
			&scene->assets.lighting_world, &light, error_out);
	}

	status = mh_race_scene_shade_finish(
		scene, field, frame, reflections, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_renderer_mesh_lit(renderer, mesh,
			scene->visuals.finish_vertices[frame->rank - 1]))
		status = mh_renderer_error(renderer, error_out);

	return status;
}
