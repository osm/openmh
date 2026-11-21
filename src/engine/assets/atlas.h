#ifndef MH_ASSETS_ATLAS_H
#define MH_ASSETS_ATLAS_H

#include "engine/assets/image.h"
#include "engine/core/error.h"
#include "engine/image/atlas_map.h"
#include "interfaces/catalog/track.h"
#include "interfaces/image/collection.h"
#include "interfaces/image/decoder.h"

enum {
	MH_ATLAS_MIP_BORDER = 2
};

struct mh_atlas_options {
	enum mh_texture_format	opaque_format, alpha_format;

	/* Shared palette zero fills gaps; individual RGB sources use black. */
	int			shared_palette;
	unsigned int		border;
};

struct mh_atlas {
	struct mh_image	*pages;

	/* Owned per-page format; NULL on unformatted diagnostic atlases. */
	unsigned char	*formats;
	size_t		count;

	/* Retained with the pages so mesh UVs use the same preparation. */
	unsigned int	border;
};

/*
 * Build all original pages in remap order, using P8/RGB565/RGB555/XRGB8888
 * for opaque pages and ARGB1555/ARGB4444 for cutouts. These describe native
 * sampling precision, not the original upload buffer layout. Select source
 * collections independently of output precision. Shared source palettes must
 * match; mixed-format pages and inconsistent source dimensions are rejected.
 * Sources must support index-zero transparency; shared palettes retain their
 * indexed RGB entries. Owns all pixels, independent of input files. Output
 * unchanged on failure. At most 16 million total page pixels. Free before
 * replacing a live atlas.
 */
enum mh_error_code	mh_atlas_build(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_atlas_map *remap,
	const struct mh_image_collection *cutout_images,
	const struct mh_image_collection *opaque_images,
	const struct mh_track *track, const struct mh_atlas_options *options,
	struct mh_atlas *output, struct mh_error *error_out);
void			mh_atlas_free(struct mh_atlas *atlas);

/*
 * Coordinate transform, including the optional two-pixel mip border. Input is
 * source pixel UV with the loader's +0.5 bias; output is normalized page UV.
 * Swap UV before scaling but do not swap the dimension pairs. Requires source
 * dimensions >=2; invalid/nonfinite inputs leave output unchanged.
 */
enum mh_error_code	mh_atlas_uv(const struct mh_atlas_map_entry *entry,
	unsigned int width, unsigned int height, unsigned int border,
	const float uv[2], float output[2], struct mh_error *error_out);

/*
 * Original nearest-center resize, optional transpose, then rectangle copy.
 * Source has already received brightness/format conversion. Source and
 * destination own disjoint RGBA storage. A two-pixel mip border performs
 * the patched ordered stamping before the page copy, using temporary images.
 * Border zero needs no allocation. Untouched page pixels and all output on
 * failure are preserved. Dimensions <=16384 and
 * images <=16 million pixels are native defensive bounds.
 */
enum mh_error_code	mh_atlas_copy(const struct mh_atlas_map_entry *entry,
	const struct mh_image *source, unsigned int border,
	struct mh_image *page, struct mh_error *error_out);

#endif
