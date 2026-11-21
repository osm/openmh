#ifndef MH_TEXTURE_POLICY_H
#define MH_TEXTURE_POLICY_H

#include "engine/assets/atlas.h"
#include "engine/render/profile.h"

/*
 * Portable atlas selection; asset loading and allocation remain shared.
 * Capacity is logical bytes, not physical VRAM. choose_atlas returns full or
 * medium quality; failures leave quality unchanged. Callers supply non-NULL
 * maps and output, plus the intended opaque format and one/three mip levels.
 */
struct mh_texture_policy {
	unsigned long		capacity;
	int			shared_palette;
	enum mh_error_code (*choose_atlas)(const struct mh_atlas_map *full,
		const struct mh_atlas_map *medium, unsigned long budget_bytes,
		enum mh_texture_format opaque_format, unsigned int levels,
		unsigned int	*quality, struct mh_error *error_out);
};

/*
 * Borrowed immutable policy. Profiles without private rules use indexed pages.
 */
const struct mh_texture_policy	*mh_scene_texture_policy(
	enum mh_render_profile profile);

#endif
