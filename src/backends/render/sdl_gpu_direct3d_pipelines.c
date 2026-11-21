#include "backends/render/sdl_gpu_pipeline_create.h"
#include "engine/render/direct3d_pipeline.h"

int
mh_gpu_direct3d_create_pipelines(
	SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set)
{
	return mh_gpu_pipeline_set_create(device, set, MH_GPU_DIRECT3D_VERTEX,
		MH_GPU_DIRECT3D_FRAGMENT, MH_GPU_DIRECT3D_GLENZ_VERTEX,
		mh_direct3d_pipeline_state);
}
