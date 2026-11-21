#include <stdio.h>
#include <string.h>

#include "engine/render/scene_effect_policy.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%d: %s\n", __LINE__, #x);             \
			return 1;                                              \
		}                                                              \
	} while (0)

struct effect_case {
	enum mh_render_profile	profile;
	int			projected, blur;
	enum mh_scene_effect	effect;
	enum mh_gpu_pipeline	pipeline;
	int	material, flat, opacity, particle_alpha, repeat, sky_sampler;
	unsigned int		primitive_vertices;
};

static int
passes(void)
{
	static const struct effect_case cases[] = {
		{MH_RENDER_GLIDE, 0, 0, MH_SCENE_EFFECT_SKY,
			MH_GPU_PIPELINE_SKY, 0, 0, 0, 0, 0, 1, 0},
		{MH_RENDER_GLIDE, 0, 1, MH_SCENE_EFFECT_SKY,
			MH_GPU_PIPELINE_BLUR_SKY, 0, 0, 0, 0, 0, 1, 0},
		{MH_RENDER_DIRECT3D, 1, 1, MH_SCENE_EFFECT_SKY,
			MH_GPU_PIPELINE_BLUR_SKY, 0, 0, 1, 0, 1, 0, 0},
		{MH_RENDER_POWERVR, 0, 1, MH_SCENE_EFFECT_SKY,
			MH_GPU_PIPELINE_OPAQUE, 0, 0, 0, 0, 0, 1, 0},
		{MH_RENDER_POWERVR, 1, 1, MH_SCENE_EFFECT_SKY,
			MH_GPU_PIPELINE_OPAQUE, 0, 0, 1, 0, 1, 0, 0},
		{MH_RENDER_GLIDE, 0, 0, MH_SCENE_EFFECT_PARTICLES,
			MH_GPU_PIPELINE_ADDITIVE, 0, 0, 0, 1, 0, 0, 0},
		{MH_RENDER_DIRECT3D, 1, 0, MH_SCENE_EFFECT_PARTICLES,
			MH_GPU_PIPELINE_ADDITIVE, 0, 1, 0, 0, 1, 0, 0},
		{MH_RENDER_POWERVR, 0, 0, MH_SCENE_EFFECT_PARTICLES,
			MH_GPU_PIPELINE_ALPHA, 0, 0, 1, 1, 0, 0, 6},
		{MH_RENDER_POWERVR, 1, 0, MH_SCENE_EFFECT_PARTICLES,
			MH_GPU_PIPELINE_ALPHA, 0, 1, 1, 0, 1, 0, 6},
		{MH_RENDER_POWERVR, 1, 0, MH_SCENE_EFFECT_HALOS,
			MH_GPU_PIPELINE_ALPHA, 0, 1, 1, 0, 1, 0, 6},
		{MH_RENDER_DIRECT3D, 1, 0, MH_SCENE_EFFECT_SHADOWS,
			MH_GPU_PIPELINE_GROUND, 0, 1, 0, 0, 1, 0, 0},
		{MH_RENDER_POWERVR, 1, 0, MH_SCENE_EFFECT_SHADOWS,
			MH_GPU_PIPELINE_GROUND, 1, 1, 0, 0, 1, 0, 0},
		{MH_RENDER_GLIDE, 0, 0, MH_SCENE_EFFECT_SKIDS,
			MH_GPU_PIPELINE_SKID, 0, 0, 0, 0, 0, 0, 0},
		{MH_RENDER_DIRECT3D, 1, 0, MH_SCENE_EFFECT_SKIDS,
			MH_GPU_PIPELINE_SKID, 1, 1, 0, 0, 1, 0, 0},
		{MH_RENDER_OPENMH, 0, 0, MH_SCENE_EFFECT_UNAVAILABLE,
			MH_GPU_PIPELINE_ADDITIVE, 1, 0, 0, 0, 0, 0, 0}};

	struct mh_effect_policy policy;
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (!mh_render_profile_available(cases[i].profile))
			continue;

		memset(&policy, 0xff, sizeof(policy));
		mh_scene_effect_policy(cases[i].profile, cases[i].projected,
			cases[i].blur, cases[i].effect, &policy);
		CHECK(policy.pipeline == cases[i].pipeline);
		CHECK(policy.material == cases[i].material);
		CHECK(policy.flat == cases[i].flat);
		CHECK(policy.opacity == cases[i].opacity);
		CHECK(policy.particle_alpha == cases[i].particle_alpha);
		CHECK(policy.repeat == cases[i].repeat);
		CHECK(policy.sky_sampler == cases[i].sky_sampler);
		CHECK(policy.primitive_vertices == cases[i].primitive_vertices);
	}

	return 0;
}

static int
order_and_fog(void)
{
	static const enum mh_scene_effect ordinary[] = {MH_SCENE_EFFECT_SKY,
		MH_SCENE_EFFECT_OPAQUE, MH_SCENE_EFFECT_SKIDS,
		MH_SCENE_EFFECT_SHADOWS, MH_SCENE_EFFECT_PARTICLES,
		MH_SCENE_EFFECT_UNAVAILABLE, MH_SCENE_EFFECT_HALOS};

	static const enum mh_scene_effect projected[] = {MH_SCENE_EFFECT_SKY,
		MH_SCENE_EFFECT_OPAQUE, MH_SCENE_EFFECT_HALOS,
		MH_SCENE_EFFECT_SKIDS, MH_SCENE_EFFECT_SHADOWS,
		MH_SCENE_EFFECT_PARTICLES, MH_SCENE_EFFECT_UNAVAILABLE};

	const enum mh_scene_effect *order;
	float color[3], result[3];
	enum mh_render_profile profile;
	unsigned int i;
	int prepared;

	for (profile = MH_RENDER_GLIDE; profile < MH_RENDER_PROFILE_COUNT;
		profile++) {
		if (!mh_render_profile_available(profile))
			continue;

		for (prepared = 0; prepared <= MH_RENDER_BUILD_DIRECT3D;
			prepared++) {
			order = mh_scene_effect_order(profile, prepared);

			for (i = 0; i < MH_SCENE_EFFECT_PASSES; i++)
				CHECK(order[i] ==
					(prepared && profile != MH_RENDER_POWERVR
							? projected[i]
							: ordinary[i]));
		}

		color[0] = 0;
		color[1] = .5f;
		color[2] = 1;
		mh_scene_fog_color(profile, color, result);
		CHECK(result[0] == 0);

		if (profile == MH_RENDER_POWERVR) {
			CHECK(result[1] == 127 / 255.0f);
			CHECK(result[2] == 254 / 255.0f);
		} else {
			CHECK(memcmp(color, result, sizeof(color)) == 0);
		}

		mh_scene_fog_color(profile, color, color);
		CHECK(memcmp(color, result, sizeof(color)) == 0);
	}

	return 0;
}

int
main(void)
{
	CHECK(passes() == 0);
	CHECK(order_and_fog() == 0);

	return 0;
}
