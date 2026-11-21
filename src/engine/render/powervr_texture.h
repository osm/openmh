#ifndef MH_POWERVR_TEXTURE_H
#define MH_POWERVR_TEXTURE_H

#include "engine/assets/image.h"

/* Square power-of-two extent accepted by PC 3.1 PowerSGL (32..256). */
enum mh_error_code	mh_powervr_texture_size(unsigned int width,
	unsigned int height, unsigned int *side, struct mh_error *error_out);

/*
 * Prepare decoded RGBA8 for native sampling at PowerSGL texture precision.
 * The only supported formats are RGB555 (original RGB upload) and ARGB4444
 * (original RGBA upload). Rows keep their top-left placement; there is no
 * resampling, dithering or mip generation. Alpha remains native opacity.
 *
 * Caller owns disjoint source/output buffers. Output must have the square
 * dimensions returned by mh_powervr_texture_size. Padding becomes black,
 * transparent for ARGB4444, opaque for RGB555; the original left it undefined.
 * Source and output pixels remain unchanged on invalid arguments.
 */
enum mh_error_code	mh_powervr_texture_prepare(
	const struct mh_image *source, enum mh_texture_format format,
	struct mh_image *output, struct mh_error *error_out);

struct mh_mesh;

/*
 * Valid mesh/draws required. Dynamic, white and environment textures stay live.
 */
int	mh_powervr_texture_unused(const struct mh_mesh *mesh, size_t texture);

#endif
