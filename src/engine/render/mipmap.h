#ifndef MH_RENDER_MIPMAP_H
#define MH_RENDER_MIPMAP_H

#include "engine/assets/image.h"

/* PC 3.1 trilinear setup requests base, half and quarter size. */
#define MH_TEXTURE_MIP_LEVELS 3

/* Select the native reduction rule for a decoded texture's original format. */
enum mh_error_code	mh_mipmap_texture(const struct mh_image *source,
	unsigned int format, unsigned int level, struct mh_image *output,
	struct mh_error *error_out);

/*
 * Average each 2^level square directly from the base RGBA8 image, truncating
 * each channel. Output dimensions must be source dimensions shifted by level,
 * with nonzero axes. Storage must be disjoint. Level is 1 or 2. Output pixels
 * remain unchanged on invalid input. This does not perform the original
 * packed-format decode/repack or P8 path.
 */
enum mh_error_code	mh_mipmap_rgba(const struct mh_image *source,
	unsigned int level, struct mh_image *output,
	struct mh_error *error_out);

/*
 * Original indexed reduction, operating on palette-expanded RGBA8. Copy the
 * top-left texel of each 2^level block directly from the base. This equals
 * selecting indices before palette expansion; no colors are averaged. Same
 * dimension/storage contract as mh_mipmap_rgba. Source pixels must already
 * carry the intended palette colors and alpha.
 */
enum mh_error_code	mh_mipmap_palette(const struct mh_image *source,
	unsigned int level, struct mh_image *output,
	struct mh_error *error_out);

/*
 * Original RGB565/XRGB8888 decode, direct base average and repack, with
 * the result expanded for native sampling. Source pixels already represent
 * that format. Same dimension/storage contract as mh_mipmap_rgba. Cutouts
 * and indexed textures are not covered by this opaque conversion path.
 */
enum mh_error_code	mh_mipmap_opaque(const struct mh_image *source,
	unsigned int level, enum mh_texture_format format,
	struct mh_image *output, struct mh_error *error_out);

#endif
