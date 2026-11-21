#ifndef MH_TEXTURE_POLICY_INTERNAL_H
#define MH_TEXTURE_POLICY_INTERNAL_H

#include "engine/render/texture_policy.h"

/* Shared logical capacity for indexed-page profiles, in bytes. */
#define MH_INDEXED_TEXTURE_CAPACITY 4194304UL

enum {
	MH_TEXTURE_QUALITY_FULL,
	MH_TEXTURE_QUALITY_MEDIUM
};

extern const struct mh_texture_policy mh_glide_texture_policy;
extern const struct mh_texture_policy mh_direct3d_texture_policy;
extern const struct mh_texture_policy mh_powervr_texture_policy;

/* Bounded page counts keep profile cost calculations within unsigned long. */
int	mh_texture_budget_valid(const struct mh_atlas_map *full,
	const struct mh_atlas_map *medium, unsigned long budget,
	const unsigned int *quality);

/* Full then medium, strictly below budget; output unchanged on failure. */
enum mh_error_code	mh_texture_quality_select(unsigned long full,
	unsigned long medium, unsigned long budget, unsigned int *quality,
	struct mh_error *error_out);

/* Indexed/1555 storage costs shared by Glide and the default texture policy. */
enum mh_error_code	mh_texture_choose_indexed(
	const struct mh_atlas_map *full, const struct mh_atlas_map *medium,
	unsigned long budget_bytes, enum mh_texture_format opaque_format,
	unsigned int levels, unsigned int *quality, struct mh_error *error_out);

#endif
