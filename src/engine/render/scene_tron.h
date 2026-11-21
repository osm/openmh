#ifndef MH_TRON_SCENE_H
#define MH_TRON_SCENE_H

#include "engine/render/scene_effect.h"
#include "engine/render/tron_draw.h"

struct mh_tron_scene_item;

struct mh_tron_scene {
	struct mh_tron_scene_item	*items;
	struct mh_renderer_tron_face	*faces;
	size_t				count, capacity, sequence;
};

/* Borrow the mesh and its latest uploaded position/lighting attributes. */
struct mh_tron_mesh {
	const struct mh_mesh	*mesh;
	const float		*positions, *colors;
	enum mh_scene_effect	effect;
	size_t			index;
};

/*
 * Zero-initialize storage. Begin retains allocations, then add visible mesh
 * spans in scene order. Finish publishes the original seven sorted passes.
 * Failure can leave partial scratch faces; begin again before another frame.
 * Sources/spans must have passed the renderer's mesh and LOD validation.
 */
void			mh_tron_scene_begin(struct mh_tron_scene *scene);
enum mh_error_code	mh_tron_scene_add(struct mh_tron_scene *scene,
	const struct mh_tron_mesh *model, const struct mh_mesh_span *span,
	const struct mh_projection *projection, const float camera[16],
	int reflections, struct mh_error *error_out);
void			mh_tron_scene_finish(struct mh_tron_scene *scene);
void			mh_tron_scene_destroy(struct mh_tron_scene *scene);

#endif
