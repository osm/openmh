#ifndef MH_WEBGL_MATERIAL_QUEUE_H
#define MH_WEBGL_MATERIAL_QUEUE_H

#include "backends/render/webgl_materials.h"

/* Internal list/storage helpers; original polygons prepend except shadows. */
enum mh_error_code	mh_webgl_material_append(
	struct mh_webgl_materials *materials, size_t model, size_t draw,
	size_t first, size_t count, size_t list, int near_clipped, int prepared,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_material_face(
	struct mh_webgl_materials *materials,
	const struct mh_webgl_scene *scene, size_t model, size_t draw,
	size_t first, size_t count, unsigned int type, const float camera[16],
	const struct mh_projection *projection, int reflections, int fog,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_material_reserve_vertices(
	struct mh_webgl_materials *materials, size_t count,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_material_upload(
	struct mh_webgl_materials *materials, struct mh_error *error_out);

#endif
