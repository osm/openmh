#include "backends/render/sdl_gpu_pipeline_create.h"
#include "engine/render/powervr_pipeline.h"

int
mh_gpu_powervr_create_pipelines(
	SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set)
{
	return mh_gpu_pipeline_set_create(device, set, MH_GPU_POWERVR_VERTEX,
		MH_GPU_POWERVR_FRAGMENT, MH_GPU_POWERVR_GLENZ_VERTEX,
		mh_powervr_pipeline_state);
}
