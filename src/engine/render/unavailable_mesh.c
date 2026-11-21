#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/unavailable_mesh.h"

#define COLOR_MAX 255.0f

enum mh_error_code
mh_unavailable_mesh_update(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double camera_position[3],
	const float model_to_world[16], const float world_to_view[16],
	double near_distance, double far_distance, struct mh_vertex *vertices,
	size_t capacity, float *material_rgb, struct mh_error *error_out)
{
	const struct mh_mesh_face *face;
	const struct mh_vertex *source;
	struct mh_vertex *vertex;
	enum mh_error_code status;
	size_t face_index, vertex_index, vertex_end;
	unsigned int axis, element, channel, k;
	double model_camera[3], world_position[3], world_normal[3],
		shaded_light[3], view_depth, facing;
	float cached_light[3], material_color[3], material_depth,
		inverse_far_distance;

	if (mesh == NULL || vertices == NULL || model_to_world == NULL ||
		world_to_view == NULL || camera_position == NULL ||
		ambient == NULL || mesh->faces == NULL ||
		mesh->vertices == NULL || vertices == mesh->vertices ||
		capacity < mesh->vertex_count ||
		!(far_distance >= 1 && far_distance <= FLT_MAX) ||
		!(near_distance >= 0 && near_distance < far_distance))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid unavailable mesh input");

	for (element = 0; element < 16; element++) {
		if (!(model_to_world[element] >= -FLT_MAX &&
			model_to_world[element] <= FLT_MAX) ||
			!(world_to_view[element] >= -FLT_MAX &&
				world_to_view[element] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid unavailable transform");
	}

	for (axis = 0; axis < 3; axis++) {
		if (!(camera_position[axis] >= -FLT_MAX &&
			camera_position[axis] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid unavailable eye");

		model_camera[axis] = 0;

		for (k = 0; k < 3; k++)
			model_camera[axis] += model_to_world[axis * 4 + k] *
				(camera_position[k] - model_to_world[12 + k]);
	}

	inverse_far_distance = (float)(1.0 / (float)far_distance);
	vertex_end = 0;

	for (face_index = 0; face_index < mesh->face_count; face_index++) {
		face = &mesh->faces[face_index];

		if (face->first != vertex_end ||
			(face->count != 3 && face->count != 6) ||
			vertex_end > mesh->vertex_count ||
			face->count > mesh->vertex_count - vertex_end) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid unavailable face span");
		}

		vertex_end += face->count;
		source = &mesh->vertices[face->first];
		facing = face->plane[3];

		for (axis = 0; axis < 3; axis++) {
			facing += model_camera[axis] * face->plane[axis];
			world_position[axis] = model_to_world[12 + axis];

			for (k = 0; k < 3; k++)
				world_position[axis] +=
					model_to_world[k * 4 + axis] *
					source->position[k];
		}

		view_depth = -world_to_view[14];

		for (axis = 0; axis < 3; axis++)
			view_depth -= world_to_view[axis * 4 + 2] *
				world_position[axis];

		if (!(view_depth >= -FLT_MAX && view_depth <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"unavailable depth overflow");

		/* Keep camera Z in near-corner cache entries. */
		material_depth =
			near_distance > 0 && (float)view_depth <= near_distance
			? (float)view_depth
			: (float)(view_depth * inverse_far_distance);

		for (vertex_index = face->first; vertex_index < vertex_end;
			vertex_index++) {
			source = &mesh->vertices[vertex_index];
			vertex = &vertices[vertex_index];
			*vertex = *source;

			if (!(facing > 0)) {
				/*
				 * Reject whole faces using their plane.
				 * Degenerate triangles keep immutable draw
				 * spans/buffers.
				 */
				memcpy(vertex->position,
					mesh->vertices[face->first].position,
					sizeof(vertex->position));

				if (material_rgb != NULL)
					memset(material_rgb + vertex_index * 3,
						0, 3 * sizeof(float));

				continue;
			}

			for (axis = 0; axis < 3; axis++) {
				world_normal[axis] = 0;

				for (k = 0; k < 3; k++)
					world_normal[axis] +=
						model_to_world[k * 4 + axis] *
						source->normal[k];
			}

			status = mh_light_shade(lights, light_count,
				world_normal, ambient, shaded_light, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			for (channel = 0; channel < 3; channel++)
				cached_light[channel] =
					(float)shaded_light[channel];

			status = mh_light_unavailable(face->color,
				material_depth, cached_light, material_color,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (material_rgb != NULL)
				memcpy(material_rgb + vertex_index * 3,
					material_color, sizeof(material_color));

			for (channel = 0; channel < 3; channel++)
				vertex->color[channel] =
					material_color[channel] / COLOR_MAX;
		}
	}

	return vertex_end == mesh->vertex_count
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete unavailable face spans");
}
