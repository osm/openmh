#ifndef MH_SCENE_EFFECT_POLICY_H
#define MH_SCENE_EFFECT_POLICY_H

#include "engine/render/pipeline.h"
#include "engine/render/profile.h"
#include "engine/render/scene_effect.h"

/* Portable pass decisions; device resources and shader packing stay outside. */
struct mh_effect_policy {
	enum mh_gpu_pipeline	pipeline;
	int			material, flat, opacity, particle_alpha;
	int			repeat, sky_sampler;
	unsigned int		primitive_vertices;
};

/*
 * Projected submission uses the fixed-function raster conventions, including
 * on PowerVR. Unprojected submission retains the shared material conventions.
 * The caller supplies a valid effect; zero primitive_vertices draws whole
 * spans.
 */
void	mh_scene_effect_policy(enum mh_render_profile profile, int projected,
	int blur, enum mh_scene_effect effect, struct mh_effect_policy *output);

/* Borrowed immutable array containing MH_SCENE_EFFECT_PASSES entries. */
const enum mh_scene_effect	*mh_scene_effect_order(
	enum mh_render_profile profile, int projected);

/* Normalized finite RGB; output may alias color. */
void	mh_scene_fog_color(enum mh_render_profile profile, const float color[3],
	float output[3]);

#endif
