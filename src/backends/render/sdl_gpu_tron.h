#ifndef MH_GPU_TRON_H
#define MH_GPU_TRON_H

#include <SDL3/SDL.h>
#include <stddef.h>

#include "engine/render/tron_draw.h"

struct mh_gpu_tron;
struct mh_renderer_tron_face;

/*
 * Private projected-polygon adapter; original numerical work is portable C.
 * The device is borrowed and must outlive this renderer. The owner waits for
 * GPU completion before destroying it. Prepare stores draws in face order;
 * draw requires a successful preparation and an index within that face count.
 * Failure may leave partial scratch draws, but never a mapped upload buffer.
 */
struct mh_gpu_tron	*mh_gpu_tron_create(SDL_GPUDevice *device,
	unsigned int width, unsigned int height);
void			 mh_gpu_tron_destroy(struct mh_gpu_tron *renderer);
int			 mh_gpu_tron_prepare(struct mh_gpu_tron *renderer,
	const struct mh_renderer_tron_face *faces, size_t count);
void			 mh_gpu_tron_draw(struct mh_gpu_tron *renderer,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass, size_t index,
	SDL_GPUTexture *texture);

#endif
