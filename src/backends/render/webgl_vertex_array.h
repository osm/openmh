#ifndef MH_WEBGL_VERTEX_ARRAY_H
#define MH_WEBGL_VERTEX_ARRAY_H

#include <GLES3/gl3.h>

#include "backends/render/shader_vertex_layout.h"
#include "engine/core/error.h"

struct mh_webgl_vertex_array {
	GLuint	handle;
};

/*
 * Caller zero-initializes output and keeps its context current. Buffers are
 * borrowed and must outlive the array; replacing their contents preserves the
 * bindings. Buffer/offset arrays have one entry per layout buffer; offsets are
 * in bytes. Creation preserves the active VAO/buffer.
 */
enum mh_error_code	mh_webgl_vertex_array_create(
	struct mh_webgl_vertex_array *output, enum mh_gpu_shader vertex_shader,
	const GLuint *buffers, const size_t *offsets,
	struct mh_error *error_out);
void			mh_webgl_vertex_array_destroy(
	struct mh_webgl_vertex_array *array);

#endif
