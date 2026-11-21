#ifndef MH_SHADER_VERTEX_LAYOUT_H
#define MH_SHADER_VERTEX_LAYOUT_H

#include <stddef.h>

#include "backends/render/shader_programs.h"

enum {
	MH_SHADER_VERTEX_BUFFERS = 2,
	MH_SHADER_VERTEX_ATTRIBUTES = 8
};

/* All attributes are unnormalized float32, advancing once per vertex. */
struct mh_shader_attribute {
	unsigned int	location, buffer, components;
	size_t		offset;
};

struct mh_shader_vertex_layout {
	unsigned int				buffer_count, attribute_count;
	size_t					strides[MH_SHADER_VERTEX_BUFFERS];
	const struct mh_shader_attribute	*attributes;
};

/* Immutable shader input contract; NULL for a non-vertex stage. */
const struct mh_shader_vertex_layout	*mh_shader_vertex_layout(
	enum mh_gpu_shader shader);

#endif
