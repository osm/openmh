#ifndef MH_WEBGL_GEOMETRY_H
#define MH_WEBGL_GEOMETRY_H

#include "backends/render/webgl_vertex_array.h"
#include "engine/render/mesh.h"
#include "interfaces/render/glenz.h"

struct mh_webgl_geometry {
	struct mh_webgl_vertex_array	array;
	GLuint				vertices, glenz, indices;
	size_t				count;
};

/*
 * Own triangle-list buffers and their VAO. Source-order vertices stay intact;
 * the index stream makes the first source corner WebGL's provoking vertex.
 * Zero-initialize output; failed create leaves it unchanged. Keep its context
 * current through destruction. Creation/uploads retain bindings, draw owns
 * the VAO. Upload validation preserves content; retry after a driver error
 * before drawing.
 */
enum mh_error_code	mh_webgl_geometry_create(
	struct mh_webgl_geometry *output, size_t count,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_geometry_vertices(
	struct mh_webgl_geometry *geometry, const struct mh_vertex *vertices,
	size_t count, struct mh_error *error_out);
enum mh_error_code	mh_webgl_geometry_glenz(
	struct mh_webgl_geometry *geometry,
	const struct mh_glenz_coordinates *coordinates, size_t count,
	struct mh_error *error_out);

/*
 * Validate and submit; the enclosing pass checks driver errors after drawing.
 */
enum mh_error_code	mh_webgl_geometry_draw(
	const struct mh_webgl_geometry *geometry, size_t first, size_t count,
	struct mh_error *error_out);
void			mh_webgl_geometry_destroy(
	struct mh_webgl_geometry *geometry);

#endif
