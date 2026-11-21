#ifndef MH_ASSETS_IMAGE_H
#define MH_ASSETS_IMAGE_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

enum mh_texture_format {
	MH_TEXTURE_PALETTE_RGB = 1,
	MH_TEXTURE_RGB565 = 3,
	MH_TEXTURE_RGB555 = 4,
	MH_TEXTURE_ARGB1555 = 5,
	MH_TEXTURE_ARGB4444 = 6,
	MH_TEXTURE_XRGB8888 = 9
};

/*
 * Original indexed palette RGB scaling applied to decoded texture pixels.
 * Alpha is unchanged. Factors are binary32, finite, in 0..100; invalid
 * input leaves the image unchanged. Not the original true-color scaler.
 */
enum mh_error_code	mh_image_texture_brightness(struct mh_image *image,
	const double rgb[3], struct mh_error *error_out);

/*
 * Prepare RGBA8 texels at the original texture precision. RGB565/555 and
 * ARGB1555 truncate channels, then expand by high-bit replication.
 * ARGB1555 retains alpha (thresholded at 128). ARGB4444 truncates all four
 * channels to four bits, then replicates the nibble; other formats are opaque.
 * Palette RGB and XRGB8888 preserve all eight color bits. This represents
 * sampling precision, not packed upload bytes. Apply after brightness.
 * Invalid format/dimensions fail without changing pixels.
 */
enum mh_error_code	mh_image_texture_format(struct mh_image *image,
	enum mh_texture_format format, struct mh_error *error_out);

#endif
