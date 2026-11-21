#include <string.h>

#include "engine/render/effect_policy_internal.h"

static void
projected_policy(
	enum mh_scene_effect effect, int blur, struct mh_effect_policy *output)
{
	output->material |= effect == MH_SCENE_EFFECT_SKIDS;

	output->flat = effect != MH_SCENE_EFFECT_SKY;
	output->opacity = blur && effect == MH_SCENE_EFFECT_SKY;
	output->particle_alpha = 0;
	output->repeat = 1;
	output->sky_sampler = 0;

	if (effect == MH_SCENE_EFFECT_SHADOWS)
		output->pipeline = MH_GPU_PIPELINE_GROUND;
}

void
mh_scene_effect_policy(enum mh_render_profile profile, int projected, int blur,
	enum mh_scene_effect effect, struct mh_effect_policy *output)
{
	memset(output, 0, sizeof(*output));

	output->material = effect == MH_SCENE_EFFECT_OPAQUE ||
		effect == MH_SCENE_EFFECT_UNAVAILABLE;
	output->sky_sampler = effect == MH_SCENE_EFFECT_SKY;
	output->particle_alpha = effect == MH_SCENE_EFFECT_PARTICLES;

	switch (effect) {
	case MH_SCENE_EFFECT_SKY:
		output->pipeline =
			blur ? MH_GPU_PIPELINE_BLUR_SKY : MH_GPU_PIPELINE_SKY;
		break;

	case MH_SCENE_EFFECT_SHADOWS:
	case MH_SCENE_EFFECT_SKIDS:
		output->pipeline = MH_GPU_PIPELINE_SKID;
		break;

	default:
		output->pipeline = MH_GPU_PIPELINE_ADDITIVE;
		break;
	}

	if (projected)
		projected_policy(effect, blur, output);

	if (MH_RENDER_BUILD_POWERVR && profile == MH_RENDER_POWERVR)
		mh_powervr_effect_policy(effect, output);
}

const enum mh_scene_effect *
mh_scene_effect_order(enum mh_render_profile profile, int projected)
{
	static const enum mh_scene_effect order[MH_SCENE_EFFECT_PASSES] = {
		MH_SCENE_EFFECT_SKY, MH_SCENE_EFFECT_OPAQUE,
		MH_SCENE_EFFECT_SKIDS, MH_SCENE_EFFECT_SHADOWS,
		MH_SCENE_EFFECT_PARTICLES, MH_SCENE_EFFECT_UNAVAILABLE,
		MH_SCENE_EFFECT_HALOS};

	if (MH_RENDER_BUILD_DIRECT3D && projected &&
		profile != MH_RENDER_POWERVR)
		return mh_direct3d_effect_order();

	return order;
}

void
mh_scene_fog_color(
	enum mh_render_profile profile, const float color[3], float output[3])
{
	unsigned int i;

	if (MH_RENDER_BUILD_POWERVR && profile == MH_RENDER_POWERVR) {
		mh_powervr_fog_color(color, output);
		return;
	}

	for (i = 0; i < 3; i++)
		output[i] = color[i];
}
