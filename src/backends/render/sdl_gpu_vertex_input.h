#ifndef MH_GPU_VERTEX_INPUT_H
#define MH_GPU_VERTEX_INPUT_H

#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/shader_vertex_layout.h"

struct mh_gpu_vertex_input {
	SDL_GPUVertexInputState		state;
	SDL_GPUVertexBufferDescription	buffers[MH_SHADER_VERTEX_BUFFERS];
	SDL_GPUVertexAttribute		attributes[MH_SHADER_VERTEX_ATTRIBUTES];
};

/* Result borrows output's arrays. NULL for a non-vertex stage. */
const SDL_GPUVertexInputState	*mh_gpu_vertex_input(enum mh_gpu_shader shader,
	struct mh_gpu_vertex_input *output);

#endif
