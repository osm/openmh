#include "engine/render/texture_policy_internal.h"

#define TEXTURE_CAPACITY 6291456UL

static enum mh_error_code
choose_atlas(const struct mh_atlas_map *full, const struct mh_atlas_map *medium,
	unsigned long budget_bytes, enum mh_texture_format opaque_format,
	unsigned int levels, unsigned int *quality, struct mh_error *error_out)
{
	(void)full;

	(void)medium;
	(void)budget_bytes;
	(void)opaque_format;
	(void)levels;

	/*
	 * PowerVR always selects the medium remap, bypassing budget
	 * comparisons.
	 */
	*quality = MH_TEXTURE_QUALITY_MEDIUM;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Logical capacity, independent of the physical GPU memory. */
const struct mh_texture_policy mh_powervr_texture_policy = {
	TEXTURE_CAPACITY, 0, choose_atlas};
