#include "backends/render/shader_uniforms.h"
#include "engine/render/material_policy.h"

void
mh_gpu_material_uniforms(const struct mh_material_policy *policy,
	float output[MH_GPU_MATERIAL_FLOATS])
{
	output[MH_GPU_MATERIAL_ENVIRONMENT] = (float)policy->environment;

	output[MH_GPU_MATERIAL_PRELIT] = (float)policy->prelit;
	output[MH_GPU_MATERIAL_FOG] = (float)policy->fog;
	output[MH_GPU_MATERIAL_ALPHA] = (float)policy->alpha;
	output[MH_GPU_MATERIAL_DISTANT] = (float)policy->distant;
	output[MH_GPU_MATERIAL_PREPARED] = (float)policy->prepared;
	output[MH_GPU_MATERIAL_FLAT] = (float)policy->flat;
	output[MH_GPU_MATERIAL_OPACITY] = (float)policy->opacity;
}
