#ifndef MH_WEBGL_SCENE_H
#define MH_WEBGL_SCENE_H

#include "backends/render/webgl_mesh.h"

struct mh_webgl_scene {
	struct mh_webgl_images		images;
	struct mh_webgl_mesh		*meshes;
	size_t				mesh_count;
	struct mh_renderer_scene_part	*parts;
	size_t				part_count;
};

/*
 * Own meshes, shared images and copied submission order; borrow source meshes.
 */
enum mh_error_code	mh_webgl_scene_open(const struct mh_mesh *const *meshes,
	size_t count, int powervr, struct mh_webgl_scene **output,
	struct mh_error *error_out);
void			mh_webgl_scene_close(struct mh_webgl_scene *scene);

/*
 * Keep meshes, texture sources, effects and submission order across GPU loss.
 */
void			mh_webgl_scene_release(struct mh_webgl_scene *scene);
enum mh_error_code	mh_webgl_scene_restore(struct mh_webgl_scene *scene,
	struct mh_error *error_out);

/*
 * Validate before replacing the retained order. NULL/zero restores mesh order.
 */
enum mh_error_code	mh_webgl_scene_order(struct mh_webgl_scene *scene,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out);

/* Revalidate active LODs and fixed spans before starting a frame. */
enum mh_error_code	mh_webgl_scene_ranges(struct mh_webgl_scene *scene,
	struct mh_error *error_out);
size_t			mh_webgl_scene_part_count(
	const struct mh_webgl_scene *scene);
size_t			mh_webgl_scene_part(const struct mh_webgl_scene *scene,
	size_t index, struct mh_mesh_span *span);

#endif
