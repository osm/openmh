#include "backends/render/sdl_gpu_pipeline_create.h"
#include "engine/render/material_pipeline.h"

int
mh_gpu_openmh_create_pipelines(
	SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set)
{
	/*
	 * OpenMH retains the material blending/depth rules, with GPU lighting.
	 */
	return mh_gpu_pipeline_set_create(device, set, MH_GPU_OPENMH_VERTEX,
		MH_GPU_MATERIAL_FRAGMENT, MH_GPU_OPENMH_VERTEX,
		mh_material_pipeline_state);
}
