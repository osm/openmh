#ifndef MH_POWERVR_FACE_H
#define MH_POWERVR_FACE_H

#include "engine/render/scene_face.h"

/*
 * PC 3.1 format 4 uses the same near splitter and post-clip RGB packing,
 * but selects distant lists after lighting/UV preparation when every corner
 * exceeds half the far distance. Distant RGB is retained, and all textured
 * materials use the distant-alpha list. background holds the .6/3.5 fade
 * byte; the consumer derives opacity from it. Visibility stays 255 because
 * SGL uses context fog rather than the producer's secondary fog bytes.
 * Environment materials are smooth. Unavailable cars have a separate path.
 */
enum mh_error_code	mh_powervr_face_prepare(
	const struct mh_projection *projection, enum mh_scene_pass material,
	unsigned int count, int smooth, const struct mh_scene_corner input[4],
	struct mh_scene_face output[2], unsigned int *output_count,
	struct mh_error *error_out);

#endif
