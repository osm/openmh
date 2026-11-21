#include <stdio.h>
#include <string.h>

#include "backends/render/shader_uniforms.h"
#include "engine/render/scene_material_policy.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%d: %s\n", __LINE__, #x);             \
			return 1;                                              \
		}                                                              \
	} while (0)

struct sampling_case {
	enum mh_render_profile	profile;
	enum mh_texture_filter	requested;
	int			glenz, environment, alpha;
	enum mh_texture_filter	expected;
	int			repeat;
};

static void
request_defaults(struct mh_material_request *request)
{
	memset(request, 0, sizeof(*request));
	request->list = MH_LIST_TEXTURE;
	request->texture = 3;
	request->environment_texture = 7;
	request->glenz_reflection = 8;
	request->glenz_phong = 9;
	request->reflections = 1;
	request->environment = 1;
	request->fog = 1;
	request->alpha_test = 1;
}

static int
material_cases(void)
{
	struct mh_material_request request;
	struct mh_material_policy policy;
	enum mh_render_profile profile;
	unsigned int i;
	float uniforms[MH_GPU_MATERIAL_FLOATS];

	static const enum mh_render_profile profiles[] = {MH_RENDER_GLIDE,
		MH_RENDER_DIRECT3D, MH_RENDER_POWERVR, MH_RENDER_OPENMH};

	for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
		profile = profiles[i];

		if (!mh_render_profile_available(profile))
			continue;

		request_defaults(&request);
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.texture == 7 && policy.environment == 1);
		CHECK(policy.pipeline == MH_GPU_PIPELINE_OPAQUE);
		CHECK(policy.fog && !policy.prelit && !policy.distant &&
			!policy.flat);
		CHECK(policy.alpha == (profile != MH_RENDER_POWERVR));
		CHECK(policy.opacity == (profile == MH_RENDER_POWERVR));

		/*
		 * Distant reflections and solid texture suppression differ by
		 * profile.
		 */
		request.list = MH_LIST_DISTANT_SOLID;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.distant);
		CHECK(policy.texture == (profile == MH_RENDER_POWERVR ? 0 : 3));
		CHECK(policy.environment == (profile == MH_RENDER_POWERVR));
		CHECK(policy.pipeline ==
			(profile == MH_RENDER_POWERVR
					? MH_GPU_PIPELINE_ALPHA
					: MH_GPU_PIPELINE_DISTANT));
		request.list = MH_LIST_DISTANT_ALPHA;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.texture == (profile == MH_RENDER_POWERVR ? 7 : 3));

		/*
		 * Phong uses its own texture/mode and never receives scene fog.
		 */
		request.list = MH_LIST_GLENZ_PHONG;
		request.glenz = 1;
		request.prepared = 1;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.pipeline == MH_GPU_PIPELINE_PHONG);
		CHECK(policy.texture == 9 && policy.environment == 2 &&
			!policy.fog);
		CHECK(policy.prepared);
		mh_gpu_material_uniforms(&policy, uniforms);
		CHECK(uniforms[MH_GPU_MATERIAL_ENVIRONMENT] == 2.0f);
		CHECK(uniforms[MH_GPU_MATERIAL_PREPARED] == 1.0f);
		CHECK(uniforms[MH_GPU_MATERIAL_FOG] == 0.0f);
		CHECK(uniforms[MH_GPU_MATERIAL_ALPHA] ==
			(profile == MH_RENDER_POWERVR ? 0.0f : 1.0f));
		CHECK(uniforms[MH_GPU_MATERIAL_OPACITY] ==
			(profile == MH_RENDER_POWERVR ? 1.0f : 0.0f));
		CHECK(uniforms[MH_GPU_MATERIAL_FLAT] == 0.0f);
		CHECK(uniforms[MH_GPU_MATERIAL_PRELIT] == 0.0f);
		CHECK(uniforms[MH_GPU_MATERIAL_DISTANT] == 0.0f);

		request.list = MH_LIST_ENVIRONMENT;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.texture == 8 &&
			policy.pipeline == MH_GPU_PIPELINE_REFLECTION);

		/*
		 * Untextured skid marks retain PowerVR fog, but no alpha test.
		 */
		request_defaults(&request);
		request.list = MH_LIST_SKID;
		request.untextured = request.skid = 1;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.texture == 0 && policy.prelit && policy.flat &&
			!policy.alpha);
		CHECK(policy.fog == (profile == MH_RENDER_POWERVR));
		CHECK(policy.pipeline ==
			(profile == MH_RENDER_POWERVR
					? MH_GPU_PIPELINE_ALPHA
					: MH_GPU_PIPELINE_GROUND));

		request_defaults(&request);
		request.shadow = 1;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(!policy.environment && policy.texture == 3 &&
			policy.flat);
		request.shadow = 0;
		request.reflections = 0;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(!policy.environment && policy.texture == 3);
		request.reflections = 1;
		request.environment_texture = 0;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(!policy.environment && policy.texture == 3);

		request_defaults(&request);
		request.list = MH_LIST_UNAVAILABLE;
		request.unavailable = 1;
		mh_scene_material_policy(profile, &request, &policy);
		CHECK(policy.flat == (profile == MH_RENDER_DIRECT3D));
		CHECK(policy.pipeline ==
			(profile == MH_RENDER_POWERVR
					? MH_GPU_PIPELINE_ALPHA
					: MH_GPU_PIPELINE_ADDITIVE));
	}

	return 0;
}

static int
sampling_cases(void)
{
	static const struct sampling_case cases[] = {
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_ORIGINAL, 0, 0, 0,
			MH_TEXTURE_FILTER_BILINEAR, 0},
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_ORIGINAL, 0, 0, 1,
			MH_TEXTURE_FILTER_NEAREST, 0},
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_TRILINEAR, 0, 0, 1,
			MH_TEXTURE_FILTER_TRILINEAR, 0},
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_TRILINEAR, 0, 1, 0,
			MH_TEXTURE_FILTER_BILINEAR, 0},
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_TRILINEAR, 1, 1, 1,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_GLIDE, MH_TEXTURE_FILTER_NEAREST, 1, 1, 0,
			MH_TEXTURE_FILTER_NEAREST, 1},
		{MH_RENDER_DIRECT3D, MH_TEXTURE_FILTER_ORIGINAL, 0, 0, 0,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_DIRECT3D, MH_TEXTURE_FILTER_TRILINEAR, 0, 0, 0,
			MH_TEXTURE_FILTER_TRILINEAR, 1},
		{MH_RENDER_DIRECT3D, MH_TEXTURE_FILTER_TRILINEAR, 0, 0, 1,
			MH_TEXTURE_FILTER_NEAREST, 1},
		{MH_RENDER_DIRECT3D, MH_TEXTURE_FILTER_TRILINEAR, 0, 1, 0,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_DIRECT3D, MH_TEXTURE_FILTER_TRILINEAR, 1, 0, 0,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_POWERVR, MH_TEXTURE_FILTER_ORIGINAL, 0, 0, 1,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_POWERVR, MH_TEXTURE_FILTER_TRILINEAR, 1, 1, 1,
			MH_TEXTURE_FILTER_BILINEAR, 1},
		{MH_RENDER_POWERVR, MH_TEXTURE_FILTER_NEAREST, 0, 0, 0,
			MH_TEXTURE_FILTER_NEAREST, 1},
		{MH_RENDER_OPENMH, MH_TEXTURE_FILTER_TRILINEAR, 0, 1, 0,
			MH_TEXTURE_FILTER_BILINEAR, 0}};

	struct mh_material_sampling sampling;
	unsigned int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (!mh_render_profile_available(cases[i].profile))
			continue;

		sampling = mh_scene_material_sampling(cases[i].profile,
			cases[i].requested, cases[i].glenz,
			cases[i].environment, cases[i].alpha);
		CHECK(sampling.filter == cases[i].expected);
		CHECK(sampling.repeat == cases[i].repeat);
	}

	return 0;
}

int
main(void)
{
	CHECK(material_cases() == 0);
	CHECK(sampling_cases() == 0);

	return 0;
}
