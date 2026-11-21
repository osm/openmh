#include "engine/render/powervr_draw.h"

void
mh_powervr_material_policy(
	const struct mh_material_request *r, struct mh_material_policy *out)
{
	int distant, phong, environment;

	distant = r->list < MH_LIST_DISTANT_COUNT;
	phong = r->list == MH_LIST_GLENZ_PHONG;

	if (distant || r->list >= MH_LIST_ALPHA)
		out->pipeline = MH_GPU_PIPELINE_ALPHA;
	else if (r->glenz)
		out->pipeline = phong ? MH_GPU_PIPELINE_PHONG
				: MH_GPU_PIPELINE_REFLECTION;
	else if (r->skid)
		out->pipeline = MH_GPU_PIPELINE_GROUND;
	else if (r->unavailable)
		out->pipeline = MH_GPU_PIPELINE_ADDITIVE;
	else
		out->pipeline = distant ? MH_GPU_PIPELINE_DISTANT
					: MH_GPU_PIPELINE_OPAQUE;

	environment = !r->untextured && !r->shadow && r->reflections &&
		r->environment && r->environment_texture != 0;
	out->environment = phong ? 2 : environment ? 1 : 0;
	out->prelit = r->untextured || r->lit || r->prelit;
	out->fog = (!r->untextured || r->skid) && !phong && r->fog;
	out->alpha = 0;
	out->distant = distant;
	out->prepared = r->prepared != 0;
	out->flat = r->skid || r->shadow;
	out->opacity = 1;
	out->texture = r->glenz ? (phong ? r->glenz_phong : r->glenz_reflection)
		: r->untextured || r->list == MH_LIST_DISTANT_SOLID ? 0
		: environment ? r->environment_texture
			: r->texture;
}

struct mh_material_sampling
mh_powervr_material_sampling(
	enum mh_texture_filter filter, int glenz, int environment, int alpha)
{
	struct mh_material_sampling result;

	(void)glenz;
	(void)environment;
	(void)alpha;

	/*
	 * PowerVR uses a repeating bilinear context, including alpha materials.
	 */
	if (filter != MH_TEXTURE_FILTER_NEAREST)
		filter = MH_TEXTURE_FILTER_BILINEAR;

	result.repeat = 1;
	result.filter = filter;

	return result;
}
