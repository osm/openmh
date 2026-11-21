#include "backends/render/sdl_gpu_pipeline_create.h"
#include "engine/render/material_pipeline.h"

int
mh_gpu_material_create_pipelines(
	SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set)
{
	return mh_gpu_pipeline_set_create(device, set, MH_GPU_MATERIAL_VERTEX,
		MH_GPU_MATERIAL_FRAGMENT, MH_GPU_MATERIAL_GLENZ_VERTEX,
		mh_material_pipeline_state);
}
