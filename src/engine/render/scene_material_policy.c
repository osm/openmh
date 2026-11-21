#include "engine/render/direct3d_draw.h"
#include "engine/render/material_policy.h"
#include "engine/render/powervr_draw.h"
#include "engine/render/scene_material_policy.h"

void
mh_scene_material_policy(enum mh_render_profile profile,
	const struct mh_material_request *request,
	struct mh_material_policy *output)
{
	if (MH_RENDER_BUILD_POWERVR && profile == MH_RENDER_POWERVR)
		mh_powervr_material_policy(request, output);

	else if (MH_RENDER_BUILD_DIRECT3D && profile == MH_RENDER_DIRECT3D)
		mh_direct3d_material_policy(request, output);
	else
		mh_material_policy(request, output);
}

struct mh_material_sampling
mh_scene_material_sampling(enum mh_render_profile profile,
	enum mh_texture_filter filter, int glenz, int environment, int alpha)
{
	if (MH_RENDER_BUILD_POWERVR && profile == MH_RENDER_POWERVR)
		return mh_powervr_material_sampling(
			filter, glenz, environment, alpha);

	if (MH_RENDER_BUILD_DIRECT3D && profile == MH_RENDER_DIRECT3D)
		return mh_direct3d_material_sampling(
			filter, glenz, environment, alpha);

	return mh_material_sampling(filter, glenz, environment, alpha);
}
