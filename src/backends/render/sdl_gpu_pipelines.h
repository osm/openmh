#ifndef MH_GPU_PIPELINES_H
#define MH_GPU_PIPELINES_H

#include <SDL3/SDL.h>

#include "engine/render/pipeline.h"

struct mh_gpu_renderer;

struct mh_gpu_pipeline_set {
	SDL_GPUGraphicsPipeline	*items[MH_GPU_PIPELINE_COUNT];
};

/*
 * Create into empty sets. The renderer owns every resulting pipeline, even
 * on failure; destroy releases and clears partially populated sets as well.
 */
int			 mh_gpu_create_pipelines(struct mh_gpu_renderer *gpu);
void			 mh_gpu_destroy_pipelines(struct mh_gpu_renderer *gpu);
SDL_GPUGraphicsPipeline	*mh_gpu_pipeline(const struct mh_gpu_renderer *gpu,
	int direct3d, enum mh_gpu_pipeline pipeline);
int			 mh_gpu_material_create_pipelines(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set);
int			 mh_gpu_direct3d_create_pipelines(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set);
int			 mh_gpu_powervr_create_pipelines(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set);

int	mh_gpu_openmh_create_pipelines(SDL_GPUDevice *device,
	struct mh_gpu_pipeline_set *set);

#endif
