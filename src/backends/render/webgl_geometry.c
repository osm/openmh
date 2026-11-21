#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_geometry.h"

static enum mh_error_code
create_buffers(struct mh_webgl_geometry *geometry, struct mh_error *error_out)
{
	GLint previous;

	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
	glGenBuffers(1, &geometry->vertices);
	glBindBuffer(GL_ARRAY_BUFFER, geometry->vertices);
	glBufferData(GL_ARRAY_BUFFER,
		geometry->count * sizeof(struct mh_vertex), NULL,
		GL_DYNAMIC_DRAW);
	glGenBuffers(1, &geometry->glenz);
	glBindBuffer(GL_ARRAY_BUFFER, geometry->glenz);
	glBufferData(GL_ARRAY_BUFFER,
		geometry->count * sizeof(struct mh_glenz_coordinates), NULL,
		GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous);

	return mh_webgl_check_error(error_out);
}

static enum mh_error_code
create_indices(struct mh_webgl_geometry *geometry, struct mh_error *error_out)
{
	GLuint *indices;
	GLint previous;
	size_t i;

	indices = malloc(geometry->count * sizeof(*indices));

	if (indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL triangle indices");

	/*
	 * A cyclic rotation preserves winding and the native 0-2 quad diagonal.
	 */
	for (i = 0; i < geometry->count; i += 3) {
		indices[i] = (GLuint)(i + 1);
		indices[i + 1] = (GLuint)(i + 2);
		indices[i + 2] = (GLuint)i;
	}

	/* WebGL classifies element buffers on first bind. Use our own VAO. */
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
	glBindVertexArray(geometry->array.handle);
	glGenBuffers(1, &geometry->indices);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		geometry->count * sizeof(*indices), indices, GL_STATIC_DRAW);
	glBindVertexArray((GLuint)previous);
	free(indices);

	return mh_webgl_check_error(error_out);
}

static enum mh_error_code
create_array(struct mh_webgl_geometry *geometry, struct mh_error *error_out)
{
	GLuint buffers[2];
	size_t offsets[2];

	buffers[0] = geometry->vertices;
	buffers[1] = geometry->glenz;
	offsets[0] = offsets[1] = 0;

	/* All three scene programs share these attributes, including Glenz. */

	return mh_webgl_vertex_array_create(&geometry->array,
		MH_GPU_MATERIAL_GLENZ_VERTEX, buffers, offsets, error_out);
}

enum mh_error_code
mh_webgl_geometry_create(struct mh_webgl_geometry *output, size_t count,
	struct mh_error *error_out)
{
	struct mh_webgl_geometry geometry;
	enum mh_error_code status;

	if (output == NULL || count == 0 || count % 3 != 0 ||
		count > INT_MAX / sizeof(struct mh_vertex))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL triangle capacity");

	if (output->array.handle != 0 || output->vertices != 0 ||
		output->glenz != 0 || output->indices != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL geometry is already created");

	memset(&geometry, 0, sizeof(geometry));
	geometry.count = count;
	status = create_buffers(&geometry, error_out);

	if (status == MH_ERROR_NONE)
		status = create_array(&geometry, error_out);

	if (status == MH_ERROR_NONE)
		status = create_indices(&geometry, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_geometry_destroy(&geometry);
		return status;
	}

	*output = geometry;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
upload(GLuint buffer, const void *data, size_t bytes,
	struct mh_error *error_out)
{
	GLint previous;

	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, data);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous);

	return mh_webgl_check_error(error_out);
}

enum mh_error_code
mh_webgl_geometry_vertices(struct mh_webgl_geometry *geometry,
	const struct mh_vertex *vertices, size_t count,
	struct mh_error *error_out)
{
	if (geometry == NULL || geometry->vertices == 0 || vertices == NULL ||
		count > geometry->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL vertex upload");

	return upload(geometry->vertices, vertices, count * sizeof(*vertices),
		error_out);
}

enum mh_error_code
mh_webgl_geometry_glenz(struct mh_webgl_geometry *geometry,
	const struct mh_glenz_coordinates *coordinates, size_t count,
	struct mh_error *error_out)
{
	if (geometry == NULL || geometry->glenz == 0 || coordinates == NULL ||
		count > geometry->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL Glenz upload");

	return upload(geometry->glenz, coordinates,
		count * sizeof(*coordinates), error_out);
}

enum mh_error_code
mh_webgl_geometry_draw(const struct mh_webgl_geometry *geometry, size_t first,
	size_t count, struct mh_error *error_out)
{
	if (geometry == NULL || geometry->array.handle == 0 || first % 3 != 0 ||
		count % 3 != 0 || first > geometry->count ||
		count > geometry->count - first)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL triangle range");

	glBindVertexArray(geometry->array.handle);
	glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT,
		(const void *)(first * sizeof(GLuint)));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_geometry_destroy(struct mh_webgl_geometry *geometry)
{
	if (geometry == NULL)
		return;

	mh_webgl_vertex_array_destroy(&geometry->array);
	glDeleteBuffers(1, &geometry->vertices);
	glDeleteBuffers(1, &geometry->glenz);
	glDeleteBuffers(1, &geometry->indices);
	memset(geometry, 0, sizeof(*geometry));
}
