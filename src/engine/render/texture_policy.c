#include "engine/core/limits.h"
#include "engine/render/texture_policy_internal.h"

#define PAGE_LIMIT 4096
#define PAGE_PIXELS 65536UL
#define RESERVED_BYTES 557056UL

int
mh_texture_budget_valid(const struct mh_atlas_map *full,
	const struct mh_atlas_map *medium, unsigned long budget,
	const unsigned int *quality)
{
	return full != NULL && medium != NULL && quality != NULL &&
		budget <= MH_U32_MAX && full->pages != 0 &&
		full->pages <= PAGE_LIMIT &&
		full->cutout_pages <= full->pages && medium->pages != 0 &&
		medium->pages <= PAGE_LIMIT &&
		medium->cutout_pages <= medium->pages;
}

enum mh_error_code
mh_texture_quality_select(unsigned long full, unsigned long medium,
	unsigned long budget, unsigned int *quality, struct mh_error *error_out)
{
	if (full < budget)
		*quality = MH_TEXTURE_QUALITY_FULL;

	else if (medium < budget)
		*quality = MH_TEXTURE_QUALITY_MEDIUM;
	else
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"insufficient logical texture memory for original atlas pages");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_texture_choose_indexed(const struct mh_atlas_map *full,
	const struct mh_atlas_map *medium, unsigned long budget_bytes,
	enum mh_texture_format opaque_format, unsigned int levels,
	unsigned int *quality, struct mh_error *error_out)
{
	unsigned long full_bytes, medium_bytes;

	(void)opaque_format;
	(void)levels;

	if (!mh_texture_budget_valid(full, medium, budget_bytes, quality))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture memory budget or page counts");

	/* Indexed/1555 pages, plus 3.5 indexed and 2.5 alpha reserve pages. */
	full_bytes = (full->pages + full->cutout_pages) * PAGE_PIXELS +
		RESERVED_BYTES;
	medium_bytes = (medium->pages + medium->cutout_pages) * PAGE_PIXELS +
		RESERVED_BYTES;

	return mh_texture_quality_select(
		full_bytes, medium_bytes, budget_bytes, quality, error_out);
}

/* Default indexed-page costs; source palettes remain independent. */
static const struct mh_texture_policy indexed_policy = {
	MH_INDEXED_TEXTURE_CAPACITY, 0, mh_texture_choose_indexed};

const struct mh_texture_policy *
mh_scene_texture_policy(enum mh_render_profile profile)
{
	switch (profile) {
#if MH_RENDER_BUILD_GLIDE
	case MH_RENDER_GLIDE:
		return &mh_glide_texture_policy;
#endif
#if MH_RENDER_BUILD_DIRECT3D
	case MH_RENDER_DIRECT3D:
		return &mh_direct3d_texture_policy;
#endif
#if MH_RENDER_BUILD_POWERVR
	case MH_RENDER_POWERVR:
		return &mh_powervr_texture_policy;
#endif
	default:
		/* Other profiles retain the shared indexed-page budget. */
		return &indexed_policy;
	}
}
