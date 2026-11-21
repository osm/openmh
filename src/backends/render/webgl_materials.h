#ifndef MH_WEBGL_MATERIALS_H
#define MH_WEBGL_MATERIALS_H

#include "backends/render/webgl_scene.h"
#include "engine/render/fog.h"
#include "engine/render/material_lists.h"
#include "engine/render/projection.h"

struct mh_webgl_primitive {
	size_t	model, draw, first, count, next;
	int	near_clipped, prepared;
};

struct mh_webgl_materials {
	/* Keep per-face material inputs in inspection draws. */
	int				openmh;
	struct mh_webgl_primitive	*items;
	size_t				count, capacity;
	size_t	heads[MH_LIST_COUNT], tails[MH_LIST_COUNT];
	struct mh_vertex		*vertices;
	struct mh_glenz_coordinates	*coordinates;
	size_t				vertex_count, vertex_capacity;
	struct mh_webgl_geometry	geometry;
};

/*
 * Own reusable CPU lists and prepared geometry. Zero-initialize before use.
 * Prepare revalidates scene spans, then rebuilds original material order and
 * uploads clipped vertices. Failure may leave partial scratch work; callers
 * must not draw it. No color/depth target is touched during preparation.
 */
enum mh_error_code	mh_webgl_materials_prepare(
	struct mh_webgl_materials *materials, struct mh_webgl_scene *scene,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_projection *projection,
	const struct mh_fog *fog, struct mh_error *error_out);
void			mh_webgl_materials_destroy(
	struct mh_webgl_materials *materials);

#endif
