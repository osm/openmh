#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/mesh.h"
#include "engine/render/tron.h"
#include "engine/ui/draw.h"
#include "engine/ui/retro.h"
#include "interfaces/render/glenz.h"
#include "tests/browser/vertices.h"

static void
upload(GLuint buffer, const void *data, size_t bytes)
{
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_STREAM_DRAW);
}

static int
select_layout(struct mh_test_webgl_vertices *vertices,
	enum mh_gpu_shader shader, size_t first_offset, size_t second_offset)
{
	size_t offsets[2];
	struct mh_error error;
	GLint previous_array, previous_buffer, array, buffer;

	mh_webgl_vertex_array_destroy(&vertices->array);
	glBindVertexArray(vertices->sentinel);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
	offsets[0] = first_offset;
	offsets[1] = second_offset;

	if (mh_webgl_vertex_array_create(&vertices->array, shader,
		vertices->buffers, offsets, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "vertex array: %s\n", error.message);
		return 0;
	}

	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
	glBindVertexArray(vertices->array.handle);

	return array == previous_array && buffer == previous_buffer;
}

void
mh_test_webgl_vertices_init(struct mh_test_webgl_vertices *vertices)
{
	memset(vertices, 0, sizeof(*vertices));
	glGenBuffers(2, vertices->buffers);
	glGenVertexArrays(1, &vertices->sentinel);
}

void
mh_test_webgl_vertices_free(struct mh_test_webgl_vertices *vertices)
{
	mh_webgl_vertex_array_destroy(&vertices->array);
	glDeleteBuffers(2, vertices->buffers);
	glDeleteVertexArrays(1, &vertices->sentinel);
}

int
mh_test_webgl_vertices_scene(struct mh_test_webgl_vertices *vertices,
	enum mh_webgl_program_kind kind, const float *triangle, int powervr)
{
	static const enum mh_gpu_shader shaders[] = {MH_GPU_MATERIAL_VERTEX,
		MH_GPU_MATERIAL_GLENZ_VERTEX, MH_GPU_DIRECT3D_VERTEX,
		MH_GPU_DIRECT3D_GLENZ_VERTEX, MH_GPU_POWERVR_VERTEX,
		MH_GPU_POWERVR_GLENZ_VERTEX};

	struct mh_vertex model[4];
	struct mh_glenz_coordinates glenz[5];
	unsigned int i;

	/*
	 * Different unused prefixes exercise independent buffer byte offsets.
	 */
	memset(model, 0, sizeof(model));
	memset(glenz, 0, sizeof(glenz));

	for (i = 0; i < 3; i++) {
		memcpy(model[i + 1].position, triangle + i * 3,
			3 * sizeof(float));
		model[i + 1].normal[2] = 1;
		model[i + 1].uv[0] = model[i + 1].uv[1] = .25f;
		model[i + 1].color[0] = model[i + 1].color[1] =
			model[i + 1].color[2] = 1;
		model[i + 1].projection_w = 1;
		model[i + 1].opacity = powervr ? 0.0f : 1.0f;
		glenz[i + 2].reflection[0] = .75f;
		glenz[i + 2].reflection[1] = .25f;
		glenz[i + 2].phong[0] = .25f;
		glenz[i + 2].phong[1] = .75f;
	}

	upload(vertices->buffers[0], model, sizeof(model));
	upload(vertices->buffers[1], glenz, sizeof(glenz));

	return select_layout(vertices, shaders[kind], sizeof(model[0]),
		2 * sizeof(glenz[0]));
}

void
mh_test_webgl_vertices_depth(
	struct mh_test_webgl_vertices *vertices, float depth)
{
	size_t offset;
	unsigned int i;

	glBindBuffer(GL_ARRAY_BUFFER, vertices->buffers[0]);

	for (i = 0; i < 3; i++) {
		offset = (i + 1) * sizeof(struct mh_vertex) +
			offsetof(struct mh_vertex, position) +
			2 * sizeof(float);
		glBufferSubData(GL_ARRAY_BUFFER, offset, sizeof(depth), &depth);
	}
}

static int
ui(struct mh_test_webgl_vertices *vertices)
{
	struct mh_ui_vertex data[3];
	unsigned int i;

	memset(data, 0, sizeof(data));
	data[1].position[0] = data[2].position[1] = 8;

	for (i = 0; i < 3; i++) {
		data[i].uv[0] = .75f;
		data[i].uv[1] = .25f;
		data[i].color[0] = data[i].color[2] = data[i].color[3] = 1;
		data[i].color[1] = .5f;
	}

	upload(vertices->buffers[0], data, sizeof(data));

	return select_layout(vertices, MH_GPU_UI_VERTEX, 0, 0);
}

static int
retro(struct mh_test_webgl_vertices *vertices)
{
	struct mh_retro_draw_vertex data[3];
	unsigned int i;

	memset(data, 0, sizeof(data));
	data[1].vertex.xy[0] = data[2].vertex.xy[1] = 8;

	for (i = 0; i < 3; i++) {
		data[i].vertex.depth = 32768;
		data[i].vertex.rgb[0] = 64;
		data[i].vertex.rgb[1] = 128;
		data[i].vertex.rgb[2] = 255;
		data[i].vertex.alpha = 255;
		data[i].uv[0] = .75f;
		data[i].uv[1] = .25f;
	}

	upload(vertices->buffers[0], data, sizeof(data));

	return select_layout(vertices, MH_GPU_RETRO_VERTEX, 0, 0);
}

static int
tron(struct mh_test_webgl_vertices *vertices)
{
	struct mh_tron_vertex data[3];
	unsigned int i;

	memset(data, 0, sizeof(data));
	data[1].line.xy[0] = data[2].line.xy[1] = 8;

	for (i = 0; i < 3; i++) {
		data[i].line.depth = 32768;
		data[i].line.rgb[0] = 255;
		data[i].line.rgb[1] = 128;
		data[i].line.rgb[2] = 64;
		data[i].line.alpha = 128;
		data[i].line.reciprocal_w = 1;
		data[i].texture_w[0] = data[i].texture_w[1] = 64;
	}

	upload(vertices->buffers[0], data, sizeof(data));

	return select_layout(vertices, MH_GPU_TRON_VERTEX, 0, 0);
}

int
mh_test_webgl_vertices_overlay(struct mh_test_webgl_vertices *vertices,
	enum mh_webgl_program_kind kind)
{
	switch (kind) {
	case MH_WEBGL_UI:
		return ui(vertices);
	case MH_WEBGL_RETRO:
		return retro(vertices);
	case MH_WEBGL_TRON:
		return tron(vertices);
	default:
		return 0;
	}
}

static int
require(int condition, const char *label)
{
	if (!condition)
		fprintf(stderr, "vertex arrays: %s failed\n", label);

	return condition;
}

int
mh_test_webgl_vertices_lifetime(struct mh_test_webgl_vertices *vertices)
{
	struct mh_webgl_vertex_array array;
	size_t offsets[2];
	GLuint buffers[2], old;
	int success;

	old = vertices->array.handle;
	offsets[0] = offsets[1] = 0;
	success = require(
		mh_webgl_vertex_array_create(&vertices->array, MH_GPU_UI_VERTEX,
			vertices->buffers, offsets, NULL) == MH_ERROR_STATE &&
			vertices->array.handle == old,
		"live array cannot be overwritten");
	mh_webgl_vertex_array_destroy(&vertices->array);
	mh_webgl_vertex_array_destroy(&vertices->array);
	success &= require(!glIsVertexArray(old) &&
			glIsBuffer(vertices->buffers[0]) &&
			glIsBuffer(vertices->buffers[1]),
		"destruction releases array and retains buffers");

	memset(&array, 0, sizeof(array));
	buffers[0] = vertices->buffers[0];
	buffers[1] = 0;
	success &= require(mh_webgl_vertex_array_create(&array,
				MH_GPU_MATERIAL_GLENZ_VERTEX, buffers,
				offsets, NULL) == MH_ERROR_INVALID,
		"missing reflection buffer is rejected");
	success &=
		require(mh_webgl_vertex_array_create(&array, MH_GPU_UI_FRAGMENT,
				buffers, offsets, NULL) == MH_ERROR_INVALID,
			"fragment shader is rejected");
	offsets[0] = 1;
	success &=
		require(mh_webgl_vertex_array_create(&array, MH_GPU_UI_VERTEX,
				buffers, offsets, NULL) == MH_ERROR_RANGE,
			"misaligned offset is rejected");
	offsets[0] = (size_t)INT_MAX + 1;
	success &=
		require(mh_webgl_vertex_array_create(&array, MH_GPU_UI_VERTEX,
				buffers, offsets, NULL) == MH_ERROR_RANGE,
			"oversized offset is rejected");
	success &= require(array.handle == 0, "invalid input leaves no array");
	mh_webgl_vertex_array_destroy(&array);

	return success;
}
