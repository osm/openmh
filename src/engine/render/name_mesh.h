#ifndef MH_RENDER_NAME_MESH_H
#define MH_RENDER_NAME_MESH_H

#include "engine/assets/name_mesh.h"
#include "engine/core/error.h"
#include "engine/render/frustum.h"

/*
 * Root positions/radii are in roster order. view is the original positive-Z
 * column-major affine transform. No allocation; publish vertices atomically.
 * Submit through the profile's unfogged, depth-tested halo pass. PowerVR
 * retains raw RGB and world reciprocal W for its transparency preparation.
 */
enum mh_error_code	mh_name_mesh_update(struct mh_name_mesh *names,
	unsigned int eligible_mask, const float *positions, const float *radii,
	const float view[12], const struct mh_projection *projection,
	const struct mh_frustum *frustum, struct mh_error *error_out);

#endif
