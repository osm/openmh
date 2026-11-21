#ifndef MH_FACE_FOGGED_H
#define MH_FACE_FOGGED_H

#include "engine/render/scene_face.h"

/*
 * Shared geometry decisions after material/lighting selection. Input material
 * is SOLID/TEXTURED/ENVIRONMENT/ALPHA/UNAVAILABLE. A far corner rejects the
 * entire polygon. Common outcodes reject it too. Distant classification and
 * alpha buckets precede near splitting. A source quad remains intact unless it
 * enters the near-clipped branch. Outputs are in per-polygon linked order, for
 * caller push-front insertion into the selected pass/texture/bucket. Errors
 * preserve outputs; culling sets count=0. Inputs and outputs may alias.
 */
enum mh_error_code	mh_fogged_face_prepare(
	const struct mh_projection *projection, enum mh_scene_pass material,
	unsigned int count, int smooth, const struct mh_scene_corner input[4],
	struct mh_scene_face output[2], unsigned int *output_count,
	struct mh_error *error_out);

#endif
