#include "engine/render/effect_policy_internal.h"

const enum mh_scene_effect *
mh_direct3d_effect_order(void)
{
	/* Glow precedes multiplying ground effects. */
	static const enum mh_scene_effect order[MH_SCENE_EFFECT_PASSES] = {
		MH_SCENE_EFFECT_SKY, MH_SCENE_EFFECT_OPAQUE,
		MH_SCENE_EFFECT_HALOS, MH_SCENE_EFFECT_SKIDS,
		MH_SCENE_EFFECT_SHADOWS, MH_SCENE_EFFECT_PARTICLES,
		MH_SCENE_EFFECT_UNAVAILABLE};

	return order;
}
