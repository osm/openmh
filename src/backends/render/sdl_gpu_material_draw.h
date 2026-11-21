#ifndef MH_GPU_MATERIAL_DRAW_H
#define MH_GPU_MATERIAL_DRAW_H

#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/shader_uniforms.h"

/* Borrows the active command/pass and updates the supplied camera scratch. */
int	mh_gpu_draw_materials(struct mh_gpu_renderer *gpu,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass,
	const float matrix[16], const float view[16], const float *models,
	int reflections, enum mh_scene_effect effect, int direct3d,
	float camera_uniform[MH_GPU_CAMERA_FLOATS]);

#endif
