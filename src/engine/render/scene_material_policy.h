#ifndef MH_SCENE_MATERIAL_POLICY_H
#define MH_SCENE_MATERIAL_POLICY_H

#include "engine/render/material_policy.h"

/* GPU scene profiles only; OpenMH uses Glide's base material rules. */
void				mh_scene_material_policy(
	enum mh_render_profile profile,
	const struct mh_material_request *request,
	struct mh_material_policy *output);
struct mh_material_sampling	mh_scene_material_sampling(
	enum mh_render_profile profile, enum mh_texture_filter filter,
	int glenz, int environment, int alpha);

#endif
