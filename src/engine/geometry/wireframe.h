#ifndef MH_ENGINE_GEOMETRY_WIREFRAME_H
#define MH_ENGINE_GEOMETRY_WIREFRAME_H

#include "engine/core/error.h"
#include "interfaces/geometry/wireframe.h"

void	mh_wireframe_free(struct mh_wireframe *mesh);

/*
 * Center, then fit by maximum coordinate. Mutates only XYZ. Zero-extent meshes
 * report scale zero. Radius 0..1e6. Invalid input leaves mesh and scale
 * unchanged.
 */
enum mh_error_code	mh_wireframe_fit(struct mh_wireframe *mesh,
	float radius, float *scale, struct mh_error *error_out);

/*
 * Center, then reuse another model's scale. Track previews fit the first track
 * to 170 and apply its scale to all others. Nonnegative scale <=1e30, output
 * XYZ within +/-1e6. Atomic failure.
 */
enum mh_error_code	mh_wireframe_center_scale(struct mh_wireframe *mesh,
	float scale, struct mh_error *error_out);

#endif
