#ifndef MH_GPU_RETRO_H
#define MH_GPU_RETRO_H

/* Private SDL boundary. Geometry, text scheduling and state live in C89. */
#include <SDL3/SDL.h>

#include "engine/ui/retro.h"

struct mh_gpu_retro;

/* Device is borrowed; the owner waits for GPU completion before destruction. */
struct mh_gpu_retro	*mh_gpu_retro_create(SDL_GPUDevice *device);
void			 mh_gpu_retro_destroy(struct mh_gpu_retro *retro);

/* Copies vertices; draw must receive the matching frame layout. */
int	mh_gpu_retro_prepare(struct mh_gpu_retro *retro,
	const struct mh_retro_draw *draw);
void	mh_gpu_retro_draw(struct mh_gpu_retro *retro,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass,
	SDL_GPUTexture *atlas, SDL_GPUSampler *sampler,
	const struct mh_retro_draw *draw);

#endif
