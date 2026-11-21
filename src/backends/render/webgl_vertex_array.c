#include <limits.h>

#include "backends/render/webgl_vertex_array.h"

static enum mh_error_code
validate_buffers(const struct mh_shader_vertex_layout *layout,
	const GLuint *buffers, const size_t *offsets,
	struct mh_error *error_out)
{
	unsigned int i;

	if (layout == NULL || buffers == NULL || offsets == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL vertex layout or buffers");

	for (i = 0; i < layout->buffer_count; i++) {
		if (buffers[i] == 0 || !glIsBuffer(buffers[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"WebGL vertex buffer is not available");

		/*
		 * Attribute offsets fit within the stride; keep pointer sums
		 * signed.
		 */
		if (offsets[i] % sizeof(float) != 0 ||
			offsets[i] > INT_MAX - layout->strides[i])
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"WebGL vertex buffer offset is invalid");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_vertex_array_create(struct mh_webgl_vertex_array *output,
	enum mh_gpu_shader vertex_shader, const GLuint *buffers,
	const size_t *offsets, struct mh_error *error_out)
{
	const struct mh_shader_vertex_layout *layout;
	const struct mh_shader_attribute *attribute;
	GLint previous_array, previous_buffer;
	GLuint array;
	size_t offset;
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL vertex array output");

	if (output->handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL vertex array is already created");

	layout = mh_shader_vertex_layout(vertex_shader);
	status = validate_buffers(layout, buffers, offsets, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	glGenVertexArrays(1, &array);

	if (array == 0)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot create WebGL vertex array");

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
	glBindVertexArray(array);

	for (i = 0; i < layout->attribute_count; i++) {
		attribute = &layout->attributes[i];
		offset = offsets[attribute->buffer] + attribute->offset;
		glBindBuffer(GL_ARRAY_BUFFER, buffers[attribute->buffer]);
		glVertexAttribPointer(attribute->location,
			(GLint)attribute->components, GL_FLOAT, GL_FALSE,
			(GLsizei)layout->strides[attribute->buffer],
			(const void *)offset);
		glEnableVertexAttribArray(attribute->location);
	}

	glBindVertexArray((GLuint)previous_array);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_buffer);
	output->handle = array;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_vertex_array_destroy(struct mh_webgl_vertex_array *array)
{
	if (array == NULL)
		return;

	glDeleteVertexArrays(1, &array->handle);
	array->handle = 0;
}
