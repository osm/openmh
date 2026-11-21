#include <string.h>

#include "backends/render/software_material.h"
#include "backends/render/software_renderer_internal.h"

#define SHADOW_COLOR 10
#define TEXTURE_EXTENT_MAX 256

static int
supported_lods(const struct mh_mesh *mesh)
{
	struct mh_mesh selected;
	struct mh_mesh_lod range;
	const struct mh_mesh_primitive *primitive;
	size_t i;
	unsigned int level;

	if (mh_mesh_active_lod(mesh, &range, NULL) != MH_ERROR_NONE)
		return 0;

	selected = *mesh;

	for (level = 0; level < mesh->lod_count; level++) {
		selected.lod = level;

		if (mh_mesh_active_lod(&selected, &range, NULL) !=
			MH_ERROR_NONE)
			return 0;

		for (i = range.primitive_first;
			i < range.primitive_first + range.primitive_count;
			i++) {
			primitive = &mesh->primitives[i];

			if (primitive->draw < range.draw_first ||
				primitive->draw - range.draw_first >=
					range.draw_count ||
				primitive->first < range.vertex_first ||
				primitive->first - range.vertex_first >
					range.vertex_count ||
				primitive->count > range.vertex_count -
						(primitive->first -
							range.vertex_first))
				return 0;
		}
	}

	return 1;
}

int
mh_software_mesh_object(const struct mh_mesh *mesh, size_t primitive,
	struct mh_mesh_object *object)
{
	size_t i, count;

	if (primitive >= object->span.primitive_first &&
		primitive - object->span.primitive_first <
			object->span.primitive_count)
		return 1;

	count = mh_mesh_scene_span_count(mesh);

	for (i = 1; i < count; i++) {
		if (!mh_mesh_scene_object(mesh, i - 1, object))
			return 0;

		if (primitive >= object->span.primitive_first &&
			primitive - object->span.primitive_first <
				object->span.primitive_count)
			return 1;
	}

	return 0;
}

int
mh_software_mesh_supported(const struct mh_mesh *mesh)
{
	const struct mh_mesh_primitive *primitive;
	const struct mh_indexed_image *image;
	const struct mh_mesh_face *face;
	struct mh_mesh_object object;
	size_t i, texture_index, used_vertices;
	unsigned int allowed_flags, type, vertex_count, channel;

	if (mesh == NULL || mesh->vertices == NULL || mesh->vertex_count == 0 ||
		mesh->vertex_count > MH_SOFTWARE_VERTEX_MAX ||
		mesh->primitives == NULL || mesh->primitive_count == 0 ||
		mesh->primitive_count > mesh->vertex_count / 3 ||
		mesh->draws == NULL || mesh->draw_count == 0 ||
		(mesh->world != NULL && !mh_mesh_world_software_ready(mesh)))
		return 0;

	if (mesh->world_colors != NULL) {
		if (mesh->world_vertex_count > mesh->vertex_count)
			return 0;

		for (channel = 0; channel < 3; channel++)
			if (!(mesh->world_brightness[channel] >= 0 &&
				mesh->world_brightness[channel] <=
					MH_SOFTWARE_BRIGHTNESS_MAX))
				return 0;
	}

	used_vertices = 0;
	memset(&object, 0, sizeof(object));

	for (i = 0; i < mesh->primitive_count; i++) {
		primitive = &mesh->primitives[i];
		type = primitive->type & ~1U;
		vertex_count = primitive->type & 1U ? 6U : 3U;

		/*
		 * Environment bits are ignored by the original type-0..7 paths.
		 */
		allowed_flags = MH_SOFTWARE_MATERIAL_DEPTH_MASK |
			MH_SOFTWARE_MATERIAL_EMISSIVE |
			MH_SOFTWARE_MATERIAL_ENVIRONMENT_MASK;

		if (type == 8)
			allowed_flags = MH_SOFTWARE_MATERIAL_FLAG_MASK;

		if (type == 2)
			allowed_flags |= MH_SOFTWARE_MATERIAL_CUTOUT |
				MH_SOFTWARE_MATERIAL_PERSPECTIVE;

		if ((type != 0 && type != 2 && type != 4 && type != 8) ||
			(primitive->flags & ~allowed_flags) != 0 ||
			primitive->count != vertex_count ||
			vertex_count > mesh->vertex_count - used_vertices ||
			primitive->draw >= mesh->draw_count ||
			primitive->first > mesh->vertex_count - vertex_count ||
			(mesh->draws[primitive->draw].environment &&
				type != 8) ||
			(mesh->draws[primitive->draw].alpha_test && type != 2))
			return 0;

		if (!mesh->draws[primitive->draw].prelit) {
			if (type == 4)
				return 0;

			if (mesh->scenery != NULL) {
				if (!mh_software_mesh_object(mesh, i, &object))
					return 0;

				face = &object.faces[i -
					object.span.primitive_first];

				if (primitive->first < object.vertex_first ||
					primitive->first - object.vertex_first >
						object.vertex_count ||
					vertex_count > object.vertex_count -
							(primitive->first -
								object.vertex_first) ||
					face->first !=
						primitive->first -
							object.vertex_first)
					return 0;
			} else {
				if (mesh->faces == NULL ||
					mesh->face_count !=
						mesh->primitive_count)
					return 0;

				face = &mesh->faces[i];

				if (face->first != primitive->first)
					return 0;
			}

			if (face->count != vertex_count ||
				face->type != primitive->type ||
				face->flags != primitive->flags)
				return 0;
		} else if (type == 8 ||
			(primitive->flags & MH_SOFTWARE_MATERIAL_EMISSIVE))
			return 0;

		used_vertices += vertex_count;

		if (type == 2) {
			texture_index = mesh->draws[primitive->draw].texture;

			if (mesh->indexed_textures == NULL ||
				texture_index >= mesh->indexed_texture_count ||
				mesh->atlas != NULL)
				return 0;

			image = &mesh->indexed_textures[texture_index];

			if (image->indices == NULL || image->width == 0 ||
				image->height == 0 ||
				image->width > TEXTURE_EXTENT_MAX ||
				image->height > TEXTURE_EXTENT_MAX ||
				image->size <
					(size_t)image->width * image->height ||
				image->colors == 0 ||
				image->colors > MH_SOFTWARE_PALETTE_COLORS)
				return 0;
		}
	}

	return supported_lods(mesh);
}

/* Software shadows are ordinary world-colored quads, not Glide multipliers. */
int
mh_software_mesh_shadow(const struct mh_mesh *mesh)
{
	const struct mh_mesh_primitive *primitive;
	size_t i;

	if (mesh->world != NULL || mesh->scenery != NULL ||
		mesh->lod_count != 0 || mesh->world_colors == NULL ||
		mesh->world_vertex_count != mesh->vertex_count)
		return 0;

	for (i = 0; i < mesh->primitive_count; i++) {
		primitive = &mesh->primitives[i];

		if (primitive->type != 1 || primitive->flags != 0 ||
			!mesh->draws[primitive->draw].prelit)
			return 0;
	}

	for (i = 0; i < mesh->vertex_count * 3; i++)
		if (mesh->world_colors[i] != SHADOW_COLOR)
			return 0;

	return 1;
}
