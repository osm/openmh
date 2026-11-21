#ifndef MH_FACE_VERTEX_COLOR_H
#define MH_FACE_VERTEX_COLOR_H

#include "engine/render/scene_face.h"

/*
 * Shared material geometry: accepts signed, unquantized RGB and packs only
 * AFTER near clipping. Material-pass RGB holds the resulting byte values;
 * distant passes use background instead and leave RGB unused. Uses the
 * binary32 distant fade start; near-split vertices have full visibility.
 * fog=0 disables distant classification and sets full visibility.
 * Errors preserve outputs; culling sets count=0, leaving faces untouched.
 * Inputs and outputs may alias. A far corner or common outcode rejects the
 * polygon. Native GPU submission must retain raw shaded RGB; normalized
 * upload colors are not equivalent. This does not implement rasterization.
 */
enum mh_error_code	mh_vertex_color_face_prepare(
	const struct mh_projection *projection, enum mh_scene_pass material,
	unsigned int count, int smooth, int fog,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, struct mh_error *error_out);

#endif
