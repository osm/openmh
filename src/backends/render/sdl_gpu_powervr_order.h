#ifndef MH_GPU_POWERVR_ORDER_H
#define MH_GPU_POWERVR_ORDER_H

#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/shader_uniforms.h"

/* PowerVR begin clears the queue, retaining reusable CPU storage. */
int	mh_gpu_powervr_order_begin(struct mh_gpu_renderer *gpu);

/*
 * Copies binding and uniform values; GPU buffers, textures and samplers remain
 * borrowed through draw. No GPU resource ownership is transferred.
 */
int	mh_gpu_powervr_order_defer(struct mh_gpu_renderer *gpu, size_t model,
	size_t first, size_t count, int prepared,
	const SDL_GPUTextureSamplerBinding *texture,
	const float camera[MH_GPU_CAMERA_FLOATS],
	const float material[MH_GPU_MATERIAL_FLOATS]);
void	mh_gpu_powervr_order_draw(struct mh_gpu_renderer *gpu,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass);
void	mh_gpu_powervr_order_destroy(struct mh_gpu_renderer *gpu);

#endif
