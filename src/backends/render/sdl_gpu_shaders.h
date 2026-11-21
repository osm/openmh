#ifndef MH_SDL_GPU_SHADERS_H
#define MH_SDL_GPU_SHADERS_H

#include <SDL3/SDL.h>

#include "backends/render/shader_programs.h"
#include "engine/core/error.h"

/* Only formats for which this build includes every shader are advertised. */
SDL_GPUShaderFormat	mh_gpu_shader_formats(void);

/*
 * Describe without creating GPU resources. Code and entrypoint borrow static
 * storage; failure leaves info unchanged.
 */
enum mh_error_code	mh_gpu_shader_info(enum mh_gpu_shader shader,
	SDL_GPUShaderFormat formats, SDL_GPUShaderCreateInfo *info,
	struct mh_error *error_out);

/* Caller releases the created shader with SDL_ReleaseGPUShader. */
SDL_GPUShader	*mh_gpu_shader_create(SDL_GPUDevice *device,
	enum mh_gpu_shader shader);

#endif
