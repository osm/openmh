#include <string.h>

#include "backends/render/webgl_material_queue.h"
#include "engine/render/scene_material_face.h"

static void
face_source(const struct mh_webgl_mesh *mesh, size_t draw, int fog,
	struct mh_material_face_source *source)
{
	const struct mh_webgl_image *image;
	size_t texture;

	memset(source, 0, sizeof(*source));
	source->mesh = mesh->source;
	source->positions = mesh->positions;
	source->colors = mesh->colors;
	source->material_rgb = mesh->material_rgb;
	source->glenz_coordinates = mesh->glenz_coordinates;
	source->effect = mesh->effect;
	source->glenz = mesh->glenz;
	source->powervr = mesh->pool->powervr;
	source->fog_enabled = fog;
	texture = mesh->source->draws[draw].texture;

	if (source->powervr && mesh->images[texture] != MH_WEBGL_NO_IMAGE) {
		image = &mesh->pool->items[mesh->images[texture]];
		source->image = image->source;
		source->texture_width = image->texture.width;
		source->texture_height = image->texture.height;
	}
}

enum mh_error_code
mh_webgl_material_face(struct mh_webgl_materials *materials,
	const struct mh_webgl_scene *scene, size_t model, size_t draw,
	size_t first, size_t count, unsigned int type, const float camera[16],
	const struct mh_projection *projection, int reflections, int fog,
	struct mh_error *error_out)
{
	struct mh_material_face_source source;
	struct mh_material_faces faces;
	enum mh_error_code status;
	size_t start, expanded;
	unsigned int face;

	face_source(&scene->meshes[model], draw, fog, &source);
	status = mh_material_face_prepare(&source, draw, first, count, type,
		camera, projection, reflections, &faces, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Prepend halves in reverse to preserve the original within-face order.
	 */
	face = faces.count;

	while (face != 0) {
		face--;
		start = materials->vertex_count;
		expanded = faces.faces[face].count == 4 ? 6 : 3;
		status = mh_webgl_material_reserve_vertices(
			materials, start + expanded, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_material_face_vertices(&source, &faces, face, projection,
			materials->vertices + start,
			materials->coordinates + start);
		materials->vertex_count += expanded;
		status = mh_webgl_material_append(materials, model, draw, start,
			expanded, faces.lists[face], faces.near_clipped, 1,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
