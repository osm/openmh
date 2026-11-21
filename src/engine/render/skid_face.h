#ifndef MH_SKID_FACE_H
#define MH_SKID_FACE_H

#include "engine/render/scene_face.h"

/*
 * PC 3.1 shared hardware ribbon producer, before renderer-specific colors
 * and blending. Direct3D uses the output fade/depth offset; PowerSGL overwrites
 * color and uses the unchanged reciprocal W instead of the biased Z.
 * Input is one projected quad; source RGB/UV are ignored. Output uses SOLID
 * faces only as an untextured geometry carrier, never ordinary scene shading.
 * Near splitting, whole-quad culling and output ownership match scene_face.
 */
enum mh_error_code	mh_skid_face_prepare(
	const struct mh_projection *projection,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, struct mh_error *error_out);

#endif
