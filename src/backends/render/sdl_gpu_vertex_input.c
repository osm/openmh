#include <string.h>

#include "backends/render/sdl_gpu_vertex_input.h"

const SDL_GPUVertexInputState *
mh_gpu_vertex_input(
	enum mh_gpu_shader shader, struct mh_gpu_vertex_input *output)
{
	static const SDL_GPUVertexElementFormat formats[] = {
		SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
		SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4};

	const struct mh_shader_vertex_layout *layout;
	const struct mh_shader_attribute *source;
	unsigned int i;

	layout = mh_shader_vertex_layout(shader);

	if (layout == NULL || output == NULL)
		return NULL;

	memset(output, 0, sizeof(*output));

	for (i = 0; i < layout->buffer_count; i++) {
		output->buffers[i].slot = i;
		output->buffers[i].pitch = (Uint32)layout->strides[i];
		output->buffers[i].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	}

	for (i = 0; i < layout->attribute_count; i++) {
		source = &layout->attributes[i];
		output->attributes[i].location = source->location;
		output->attributes[i].buffer_slot = source->buffer;
		output->attributes[i].format = formats[source->components - 1];
		output->attributes[i].offset = (Uint32)source->offset;
	}

	output->state.vertex_buffer_descriptions = output->buffers;
	output->state.num_vertex_buffers = layout->buffer_count;
	output->state.vertex_attributes = output->attributes;
	output->state.num_vertex_attributes = layout->attribute_count;

	return &output->state;
}
