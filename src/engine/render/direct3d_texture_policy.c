#include "engine/render/texture_policy_internal.h"

#define TEXTURE_CAPACITY 33554432UL

#define PAGE_PIXELS 65536UL
#define OPAQUE_RESERVE_PAGES 6UL
#define ALPHA_RESERVE_BYTES (5UL * PAGE_PIXELS)

static unsigned long
format_budget(const struct mh_atlas_map *remap, unsigned int pixel_bytes,
	unsigned int levels)
{
	unsigned long bytes, remainder;

	bytes = ((remap->pages - remap->cutout_pages) * pixel_bytes +
			remap->cutout_pages * 2) *
		PAGE_PIXELS;

	if (levels > 1) {
		/*
		 * Binary64 1.4 multiplied in extended precision, then
		 * truncated. It lies just below 7/5, so exact multiples of five
		 * lose one byte. Quotient/remainder avoids overflow and does
		 * not depend on the host's long double precision.
		 */
		remainder = bytes % 5;
		bytes = bytes / 5 * 7 + remainder * 7 / 5 - (remainder == 0);
	}

	return bytes + OPAQUE_RESERVE_PAGES * pixel_bytes * PAGE_PIXELS +
		ALPHA_RESERVE_BYTES;
}

static enum mh_error_code
choose_atlas(const struct mh_atlas_map *full, const struct mh_atlas_map *medium,
	unsigned long budget_bytes, enum mh_texture_format opaque_format,
	unsigned int levels, unsigned int *quality, struct mh_error *error_out)
{
	unsigned int pixel_bytes;

	if (!mh_texture_budget_valid(full, medium, budget_bytes, quality) ||
		(opaque_format != MH_TEXTURE_PALETTE_RGB &&
			opaque_format != MH_TEXTURE_RGB565 &&
			opaque_format != MH_TEXTURE_XRGB8888) ||
		(levels != 1 && levels != 3))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid formatted atlas memory budget");

	pixel_bytes = opaque_format == MH_TEXTURE_PALETTE_RGB ? 1
		: opaque_format == MH_TEXTURE_RGB565	      ? 2
							: 4;

	return mh_texture_quality_select(
		format_budget(full, pixel_bytes, levels),
		format_budget(medium, pixel_bytes, levels), budget_bytes,
		quality, error_out);
}

/* Logical capacity, independent of the physical GPU memory. */
const struct mh_texture_policy mh_direct3d_texture_policy = {
	TEXTURE_CAPACITY, 0, choose_atlas};
