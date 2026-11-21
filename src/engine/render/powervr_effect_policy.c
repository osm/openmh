#include "engine/render/effect_policy_internal.h"

#define FOG_COLOR_MAX 255.0
#define FOG_FIXED_SCALE 256.0
#define FOG_COLOR_NORMALIZER 255.0f

enum {
	EFFECT_QUAD_VERTICES = 6
};

void
mh_powervr_effect_policy(
	enum mh_scene_effect effect, struct mh_effect_policy *output)
{
	output->material |= effect == MH_SCENE_EFFECT_SHADOWS;

	if (effect == MH_SCENE_EFFECT_SKY)
		output->pipeline = MH_GPU_PIPELINE_OPAQUE;

	if (effect == MH_SCENE_EFFECT_PARTICLES ||
		effect == MH_SCENE_EFFECT_HALOS) {
		output->pipeline = MH_GPU_PIPELINE_ALPHA;
		output->opacity = 1;

		/* Each effect quad is one ordered PowerSGL submission. */
		output->primitive_vertices = EFFECT_QUAD_VERTICES;
	}
}

void
mh_powervr_fog_color(const float color[3], float output[3])
{
	unsigned int i;

	/*
	 * Truncate the fixed-point fog color before normalizing its byte value.
	 */
	for (i = 0; i < 3; i++)
		output[i] = (unsigned int)(color[i] *
				(FOG_COLOR_MAX * FOG_COLOR_MAX /
					FOG_FIXED_SCALE)) /
			FOG_COLOR_NORMALIZER;
}
