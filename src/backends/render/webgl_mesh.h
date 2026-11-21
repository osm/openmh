#ifndef MH_WEBGL_MESH_H
#define MH_WEBGL_MESH_H

#include "backends/render/webgl_geometry.h"
#include "backends/render/webgl_images.h"
#include "engine/render/openmh_light_buffer.h"
#include "engine/render/scene_effect.h"

struct mh_webgl_mesh {
	struct mh_openmh_light_buffer	openmh;
	const struct mh_mesh		*source;
	const struct mh_webgl_images	*pool;
	const size_t			*images;
	struct mh_webgl_geometry	geometry;
	struct mh_mesh_lod		range;
	struct mh_vertex		*upload;
	struct mh_vertex		*encoding;
	size_t				vertex_count;
	float	*positions, *colors, *projection_w, *material_rgb;
	struct mh_glenz_coordinates	*glenz_coordinates;
	size_t				glenz_reflection, glenz_phong;
	enum mh_scene_effect		effect;
	int				hidden, lit, glenz;
	int				vertices_pending, glenz_pending;
};

/*
 * Own buffers/cached attributes; borrow source and image pool through destroy.
 * Source must be the mesh at model in the pool's original source list.
 */
enum mh_error_code	mh_webgl_mesh_create(struct mh_webgl_mesh *output,
	const struct mh_mesh *source, const struct mh_webgl_images *pool,
	size_t model, struct mh_error *error_out);

/*
 * NULL vertices uses the source mesh. Apply atlas/effect encoding only to the
 * staged copy; retain unmodified attributes for CPU face preparation. Updates
 * perform no GPU calls. Vertex capacity is fixed; active draws/LOD may change.
 * Submit pending uploads before drawing; a failed batch remains pending and
 * must be retried successfully before drawing any of its meshes.
 */
enum mh_error_code	mh_webgl_mesh_update(struct mh_webgl_mesh *mesh,
	const struct mh_vertex *vertices, enum mh_scene_effect effect, int lit,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_mesh_glenz(struct mh_webgl_mesh *mesh,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out);

/*
 * Upload pending vertices/coordinates with one binding save and driver check.
 */
enum mh_error_code	mh_webgl_meshes_upload(struct mh_webgl_mesh *meshes,
	size_t count, struct mh_error *error_out);
void			mh_webgl_mesh_destroy(struct mh_webgl_mesh *mesh);

/* Recreate released geometry from the latest staged attributes/effects. */
enum mh_error_code	mh_webgl_mesh_restore(struct mh_webgl_mesh *mesh,
	struct mh_error *error_out);

#endif
