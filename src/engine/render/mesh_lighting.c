#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/material_color.h"
#include "engine/render/mesh_internal.h"
#include "engine/render/object_light.h"

#define BRIGHTNESS_LIMIT 200

enum mh_error_code
mh_mesh_prepare_material_rgb(struct mh_mesh *mesh, struct mh_error *error_out)
{
	float *colors;
	size_t i;
	unsigned int axis;

	if (mesh == NULL || mesh->vertices == NULL || mesh->vertex_count == 0 ||
		mesh->vertex_count > (size_t)-1 / (3 * sizeof(float)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid raw material color storage");

	if (mesh->material_rgb != NULL &&
		mesh->material_rgb_count == mesh->vertex_count)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	colors = (float *)malloc(mesh->vertex_count * 3 * sizeof(*colors));

	if (colors == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot retain raw material colors");

	for (i = 0; i < mesh->vertex_count; i++)
		for (axis = 0; axis < 3; axis++)
			colors[i * 3 + axis] =
				mesh->vertices[i].color[axis] * 255;

	free(mesh->material_rgb);
	mesh->material_rgb = colors;
	mesh->material_rgb_count = mesh->vertex_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
world_brightness(struct mh_mesh *mesh, const double brightness[3],
	int retain_material, struct mh_error *error_out)
{
	size_t vertex;
	unsigned int axis;
	double value;
	float color[3];
	unsigned long packed;
	enum mh_error_code status;

	if (mesh == NULL || brightness == NULL || mesh->vertices == NULL ||
		mesh->world_colors == NULL ||
		mesh->world_vertex_count > mesh->vertex_count ||
		mesh->world_vertex_count > (size_t)-1 / 3)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world color source");

	for (axis = 0; axis < 3; axis++)
		if (!(brightness[axis] >= 0 &&
			brightness[axis] <= BRIGHTNESS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid world brightness");

	if (retain_material) {
		status = mh_mesh_prepare_material_rgb(mesh, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (axis = 0; axis < 3; axis++)
		mesh->world_brightness[axis] = (float)brightness[axis];

	for (vertex = 0; vertex < mesh->world_vertex_count; vertex++) {
		if (retain_material) {
			status = mh_prelit_color(
				mesh->world_colors + vertex * 3,
				mesh->world_brightness, color, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_material_color_pack(
					color, &packed, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			memcpy(mesh->material_rgb + vertex * 3, color,
				sizeof(color));

			for (axis = 0; axis < 3; axis++)
				mesh->vertices[vertex].color[axis] =
					(float)((packed >> (16 - axis * 8)) &
						255) /
					255.0f;

			continue;
		}

		for (axis = 0; axis < 3; axis++) {
			value = mesh->world_colors[vertex * 3 + axis] *
				brightness[axis];
			mesh->vertices[vertex].color[axis] =
				(float)((value > 255 ? 255 : value) / 255);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_world_brightness(struct mh_mesh *mesh, const double brightness[3],
	struct mh_error *error_out)
{
	return world_brightness(mesh, brightness, 0, error_out);
}

enum mh_error_code
mh_mesh_material_world_brightness(struct mh_mesh *mesh,
	const double brightness[3], struct mh_error *error_out)
{
	return world_brightness(mesh, brightness, 1, error_out);
}

static enum mh_error_code
light_scene(struct mh_mesh *mesh, const struct mh_world *world,
	const double ambient[3], const double brightness[3], double intensity,
	const double eye[3], struct mh_prepared_light *scratch, size_t capacity,
	int retain_material, struct mh_error *error_out)
{
	struct mh_mesh_binding *binding;
	const struct mh_scene_object *pose;
	struct mh_point_light light;
	struct mh_mesh local;
	float model[16], *material_rgb;
	const double *rotation;
	size_t cursor;
	unsigned int axis, column;
	size_t light_count, vertex;
	double color;
	enum mh_error_code status;

	if (mesh == NULL || world == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing scenery lighting");

	if (mesh->scenery == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (retain_material) {
		status = mh_mesh_prepare_material_rgb(mesh, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (mesh->scenery->fit_radius != 1 ||
		mesh->scenery->fit_center[0] != 0 ||
		mesh->scenery->fit_center[1] != 0 ||
		mesh->scenery->fit_center[2] != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"scenery lighting requires native geometry");

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next) {
		pose = &binding->pose;
		rotation = pose->rotation;
		status = mh_world_lights_begin(
			world, pose->position, &cursor, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		light_count = 0;

		while (mh_world_light_next(world, &cursor, &light)) {
			if (scratch == NULL || light_count >= capacity)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"scenery light scratch too small");

			status = mh_light_prepare(&light, pose->position, eye,
				intensity, scratch + light_count++, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		memset(model, 0, sizeof(model));
		model[15] = 1;

		for (axis = 0; axis < 3; axis++) {
			model[12 + axis] = (float)pose->position[axis];

			for (column = 0; column < 3; column++)
				model[column * 4 + axis] =
					(float)rotation[column * 3 + axis];
		}

		memset(&local, 0, sizeof(local));
		local.vertices = binding->local_vertices;
		local.vertex_count = binding->vertex_count;
		local.faces = binding->faces;
		local.face_count = binding->face_count;
		material_rgb = retain_material
			? mesh->material_rgb + binding->vertex_first * 3
			: NULL;

		if (retain_material)
			status = mh_mesh_material_light(&local, scratch,
				light_count, ambient, brightness, model,
				MH_LIGHT_OBJECT, 1, binding->staged_vertices,
				binding->vertex_count, material_rgb, error_out);
		else
			status = mh_mesh_object_light(&local, scratch,
				light_count, ambient, brightness, model,
				MH_LIGHT_OBJECT, binding->staged_vertices,
				binding->vertex_count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (vertex = 0; vertex < binding->vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++) {
				if (material_rgb != NULL)
					material_rgb[vertex * 3 + axis] *=
						(float)pose->color[axis];

				color = binding->staged_vertices[vertex]
						.color[axis] *
					pose->color[axis];

				if (color < 0)
					color = 0;

				if (color > 1)
					color = 1;

				binding->staged_vertices[vertex].color[axis] =
					(float)color;
			}
	}

	/* Publish colors only: animation/physics positions stay untouched. */
	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next)
		for (vertex = 0; vertex < binding->vertex_count; vertex++)
			memcpy(mesh->vertices[binding->vertex_first + vertex]
					.color,
				binding->staged_vertices[vertex].color,
				3 * sizeof(float));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_light_scene(struct mh_mesh *mesh, const struct mh_world *world,
	const double ambient[3], const double brightness[3], double intensity,
	const double eye[3], struct mh_prepared_light *scratch, size_t capacity,
	struct mh_error *error_out)
{
	return light_scene(mesh, world, ambient, brightness, intensity, eye,
		scratch, capacity, 0, error_out);
}

enum mh_error_code
mh_mesh_material_light_scene(struct mh_mesh *mesh, const struct mh_world *world,
	const double ambient[3], const double brightness[3], double intensity,
	const double eye[3], struct mh_prepared_light *scratch, size_t capacity,
	struct mh_error *error_out)
{
	return light_scene(mesh, world, ambient, brightness, intensity, eye,
		scratch, capacity, 1, error_out);
}
