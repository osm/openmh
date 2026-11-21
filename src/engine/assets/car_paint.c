#include "engine/assets/car_paint.h"
#include "engine/core/error.h"

#define RGB_MASK 0xffffffUL
#define ENVIRONMENT_FLAGS 0x60

enum {
	ENVIRONMENT_TRIANGLE = 8,
	ENVIRONMENT_QUAD = 9
};

enum mh_error_code
mh_car_paint_apply(struct mh_mesh *mesh,
	const unsigned long source_keys[MH_CAR_PAINT_KEYS],
	const unsigned long target_colors[MH_CAR_PAINT_KEYS],
	struct mh_error *error_out)
{
	struct mh_mesh_face *face;
	unsigned long packed_rgb;
	unsigned int i, channel;
	size_t face_index, vertex_index, vertex_end;

	if (mesh == NULL || source_keys == NULL || target_colors == NULL ||
		(mesh->face_count != 0 && mesh->faces == NULL) ||
		(mesh->vertex_count != 0 && mesh->vertices == NULL))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid car paint mesh");

	for (i = 0; i < MH_CAR_PAINT_KEYS; i++)
		if (source_keys[i] > RGB_MASK || target_colors[i] > RGB_MASK)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car paint color");

	vertex_end = 0;

	for (face_index = 0; face_index < mesh->face_count; face_index++) {
		face = &mesh->faces[face_index];

		if (face->type >= MH_GEOMETRY_FACE_TYPES ||
			face->first != vertex_end ||
			(face->count != 3 && face->count != 6) ||
			vertex_end > mesh->vertex_count ||
			face->count > mesh->vertex_count - vertex_end)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car paint face span");

		vertex_end += face->count;
	}

	if (vertex_end != mesh->vertex_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete car paint face spans");

	for (face_index = 0; face_index < mesh->face_count; face_index++) {
		face = &mesh->faces[face_index];

		if ((face->type != ENVIRONMENT_TRIANGLE &&
			face->type != ENVIRONMENT_QUAD) ||
			!(face->flags & ENVIRONMENT_FLAGS))
			continue;

		packed_rgb = ((unsigned long)face->color[0] << 16) |
			((unsigned long)face->color[1] << 8) | face->color[2];

		/*
		 * Each comparison reads the result of the preceding
		 * replacement.
		 */
		for (i = 0; i < MH_CAR_PAINT_KEYS; i++)
			if (packed_rgb == source_keys[i])
				packed_rgb = target_colors[i];

		for (channel = 0; channel < 3; channel++) {
			face->color[channel] =
				(unsigned char)((packed_rgb >>
							(16 - channel * 8)) &
					255UL);

			for (vertex_index = face->first;
				vertex_index < face->first + face->count;
				vertex_index++)
				mesh->vertices[vertex_index].color[channel] =
					face->color[channel] / 255.0f;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
