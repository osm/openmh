#ifndef MH_MATERIAL_CLASSIFY_H
#define MH_MATERIAL_CLASSIFY_H
#include "engine/core/error.h"
#include "engine/render/material_lists.h"
struct mh_mesh;

/*
 * Borrowed CPU geometry and policy; no device resources. Keep inverse_far
 * separate from fog_enabled: alpha buckets use depths even without fog.
 */
struct mh_material_classification {
	const struct mh_mesh	*mesh;
	const float		*positions;
	float			inverse_far;
	int			fog_enabled, glenz, reflections, unavailable;
};

/*
 * Caller validates draw and vertex ranges. Count is 3 (triangle) or 6 (quad
 * triangulation); the quad corners are 0,1,2,5. Unavailable material bypasses
 * depth classification. Exact near-plane equality suppresses Phong but does
 * not enter the negative-depth mask. Output near_clipped starts at zero.
 * Group validation occurs only when group-based classification is needed.
 */
enum mh_error_code	mh_material_classify(
	const struct mh_material_classification *state, size_t draw_index,
	size_t first, size_t count, const float view[16], const float clip[16],
	size_t *list, int *near_clipped, struct mh_error *error_out);
#endif
