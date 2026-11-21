#ifndef MH_EFFECT_POLICY_INTERNAL_H
#define MH_EFFECT_POLICY_INTERNAL_H

#include "engine/render/scene_effect_policy.h"

/* Profile overrides receive a complete shared policy. */
const enum mh_scene_effect	*mh_direct3d_effect_order(void);

void	mh_powervr_effect_policy(enum mh_scene_effect effect,
	struct mh_effect_policy *output);
void	mh_powervr_fog_color(const float color[3], float output[3]);

#endif
