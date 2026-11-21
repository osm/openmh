#include <stddef.h>

#include "backends/render/sdl_gpu_pipelines.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

int
mh_gpu_create_pipelines(struct mh_gpu_renderer *gpu)
{
#if MH_RENDER_BUILD_POWERVR
	if (gpu->powervr)
		return mh_gpu_powervr_create_pipelines(
			gpu->device, &gpu->powervr_pipelines);

#endif
	if (!mh_gpu_material_create_pipelines(
		gpu->device, &gpu->material_pipelines))
		return 0;

#if MH_RENDER_BUILD_DIRECT3D
	if (!mh_gpu_direct3d_create_pipelines(
		gpu->device, &gpu->direct3d_pipelines))
		return 0;
#endif

	return 1;
}

static void
destroy_set(SDL_GPUDevice *device, struct mh_gpu_pipeline_set *set)
{
	unsigned int pipeline;

	for (pipeline = 0; pipeline < MH_GPU_PIPELINE_COUNT; pipeline++) {
		if (set->items[pipeline] != NULL)
			SDL_ReleaseGPUGraphicsPipeline(
				device, set->items[pipeline]);

		set->items[pipeline] = NULL;
	}
}

void
mh_gpu_destroy_pipelines(struct mh_gpu_renderer *gpu)
{
	destroy_set(gpu->device, &gpu->material_pipelines);
	destroy_set(gpu->device, &gpu->openmh_pipelines);
	destroy_set(gpu->device, &gpu->direct3d_pipelines);
	destroy_set(gpu->device, &gpu->powervr_pipelines);
}

SDL_GPUGraphicsPipeline *
mh_gpu_pipeline(const struct mh_gpu_renderer *gpu, int direct3d,
	enum mh_gpu_pipeline pipeline)
{
	if (gpu->powervr)
		return gpu->powervr_pipelines.items[pipeline];

	if (direct3d)
		return gpu->direct3d_pipelines.items[pipeline];

	return gpu->material_pipelines.items[pipeline];
}
