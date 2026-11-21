#ifndef MH_POWERVR_SHADOW_H
#define MH_POWERVR_SHADOW_H

#include "engine/render/scene_face.h"

/*
 * Caller supplies valid projection/corner pointers and disjoint outputs.
 * Depth-plane or viewport rejection sets used=0 and leaves face untouched.
 * Preparation errors keep used=0 but may leave face partially written.
 */
enum mh_error_code	mh_powervr_shadow_prepare(
	const struct mh_projection *projection,
	const struct mh_scene_corner input[4], struct mh_scene_face *face,
	unsigned int *used, struct mh_error *error_out);

#endif
