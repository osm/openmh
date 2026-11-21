#include <stddef.h>

#include "backends/render/shader_programs.h"
#include "backends/render/shader_uniforms.h"
#include "backends/render/webgl_program.h"
#include "engine/render/openmh_light_buffer.h"

struct shader_source {
	enum mh_gpu_shader	shader;
	const unsigned char	*bytes;
};

#include "shader_sources.h"

struct program_stages {
	enum mh_gpu_shader	vertex, fragment;
	const char		*parameters, *sampler;
	GLint			parameter_bytes, material_bytes;
	GLuint			material_binding;
};

static const struct program_stages programs[MH_WEBGL_PROGRAM_COUNT] = {
	{MH_GPU_MATERIAL_VERTEX, MH_GPU_MATERIAL_FRAGMENT, "Camera", "diffuse",
		MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_MATERIAL_GLENZ_VERTEX, MH_GPU_MATERIAL_FRAGMENT, "Camera",
		"diffuse", MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_DIRECT3D_VERTEX, MH_GPU_DIRECT3D_FRAGMENT, "Camera", "diffuse",
		MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_DIRECT3D_GLENZ_VERTEX, MH_GPU_DIRECT3D_FRAGMENT, "Camera",
		"diffuse", MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_POWERVR_VERTEX, MH_GPU_POWERVR_FRAGMENT, "Camera", "diffuse",
		MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_POWERVR_GLENZ_VERTEX, MH_GPU_POWERVR_FRAGMENT, "Camera",
		"diffuse", MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL},
	{MH_GPU_UI_VERTEX, MH_GPU_UI_FRAGMENT, "Viewport", "atlas", 16, 0, 0},
	{MH_GPU_TRON_VERTEX, MH_GPU_TRON_FRAGMENT, "Parameters", "diffuse", 16,
		16, MH_WEBGL_FRAGMENT_MATERIAL},
	{MH_GPU_RETRO_VERTEX, MH_GPU_RETRO_FRAGMENT, "Parameters", "atlas", 16,
		16, MH_WEBGL_FRAGMENT_MATERIAL},
	{MH_GPU_DEPTH_VERTEX, MH_GPU_DEPTH_FRAGMENT, "Parameters",
		"depth_image", 16, 0, 0},
	{MH_GPU_OPENMH_VERTEX, MH_GPU_MATERIAL_FRAGMENT, "Camera", "diffuse",
		MH_GPU_CAMERA_FLOATS * 4, MH_GPU_MATERIAL_FLOATS * 4,
		MH_WEBGL_VERTEX_MATERIAL}};

static const char *
shader_text(enum mh_gpu_shader shader)
{
	size_t i;

	for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++)

		if (sources[i].shader == shader)
			return (const char *)sources[i].bytes;

	return NULL;
}

int
mh_webgl_program_available(enum mh_webgl_program_kind kind)
{
	return kind >= 0 && kind < MH_WEBGL_PROGRAM_COUNT &&
		shader_text(programs[kind].vertex) != NULL &&
		shader_text(programs[kind].fragment) != NULL;
}

static enum mh_error_code
compile_stage(struct mh_webgl_program *program, enum mh_gpu_shader shader,
	GLenum stage, GLuint *output, struct mh_error *error_out)
{
	const char *source;
	GLuint handle;
	GLint compiled;

	source = shader_text(shader);

	if (source == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"WebGL shader source is missing");

	handle = glCreateShader(stage);

	if (handle == 0)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot create WebGL shader");

	glShaderSource(handle, 1, &source, NULL);
	glCompileShader(handle);
	glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);

	if (!compiled) {
		glGetShaderInfoLog(handle, sizeof(program->diagnostic), NULL,
			program->diagnostic);
		glDeleteShader(handle);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			program->diagnostic[0] ? program->diagnostic
					: "cannot compile WebGL shader");
	}

	*output = handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
link_stages(struct mh_webgl_program *program,
	const struct program_stages *stages, struct mh_error *error_out)
{
	GLuint vertex, fragment;
	GLint linked;
	enum mh_error_code status;

	vertex = fragment = 0;
	status = compile_stage(
		program, stages->vertex, GL_VERTEX_SHADER, &vertex, error_out);

	if (status == MH_ERROR_NONE)
		status = compile_stage(program, stages->fragment,
			GL_FRAGMENT_SHADER, &fragment, error_out);

	if (status == MH_ERROR_NONE) {
		program->handle = glCreateProgram();

		if (program->handle == 0)
			status = mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot create WebGL program");
	}

	if (status == MH_ERROR_NONE) {
		glAttachShader(program->handle, vertex);
		glAttachShader(program->handle, fragment);
		glLinkProgram(program->handle);
		glGetProgramiv(program->handle, GL_LINK_STATUS, &linked);
		glDetachShader(program->handle, vertex);
		glDetachShader(program->handle, fragment);

		if (!linked) {
			glGetProgramInfoLog(program->handle,
				sizeof(program->diagnostic), NULL,
				program->diagnostic);
			status = mh_error_set(error_out, MH_ERROR_BACKEND,
				program->diagnostic[0]
					? program->diagnostic
					: "cannot link WebGL program");
		}
	}

	glDeleteShader(vertex);
	glDeleteShader(fragment);

	return status;
}

static enum mh_error_code
bind_block(GLuint program, const char *name, GLint bytes, GLuint binding,
	struct mh_error *error_out)
{
	GLuint index;
	GLint actual;

	index = glGetUniformBlockIndex(program, name);

	if (index == GL_INVALID_INDEX)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebGL program is missing a uniform block");

	glGetActiveUniformBlockiv(
		program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &actual);

	if (actual != bytes)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebGL uniform block has an unexpected size");

	glUniformBlockBinding(program, index, binding);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
bind_resources(GLuint program, const struct program_stages *stages,
	struct mh_error *error_out)
{
	GLint sampler, previous;
	enum mh_error_code status;

	status = bind_block(program, stages->parameters,
		stages->parameter_bytes, MH_WEBGL_VERTEX_PARAMETERS, error_out);

	if (status == MH_ERROR_NONE && stages->material_bytes != 0)
		status = bind_block(program, "Material", stages->material_bytes,
			stages->material_binding, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sampler = glGetUniformLocation(program, stages->sampler);

	if (sampler < 0)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"WebGL program is missing its texture sampler");

	glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
	glUseProgram(program);
	glUniform1i(sampler, MH_WEBGL_TEXTURE);
	glUseProgram((GLuint)previous);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_program_create(struct mh_webgl_program *program,
	enum mh_webgl_program_kind kind, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (program == NULL || kind < 0 || kind >= MH_WEBGL_PROGRAM_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL program request");

	if (program->handle != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL program is already created");

	program->diagnostic[0] = '\0';
	status = link_stages(program, &programs[kind], error_out);

	if (status == MH_ERROR_NONE)
		status = bind_resources(
			program->handle, &programs[kind], error_out);

	if (status == MH_ERROR_NONE && kind == MH_WEBGL_OPENMH)
		status = bind_block(program->handle, "Lighting",
			MH_OPENMH_LIGHT_FLOATS * 4, MH_WEBGL_LIGHTING,
			error_out);

	if (status == MH_ERROR_NONE && kind == MH_WEBGL_OPENMH)
		status = bind_block(program->handle, "Surface", 16,
			MH_WEBGL_SURFACE, error_out);

	if (status != MH_ERROR_NONE)
		mh_webgl_program_destroy(program);

	return status;
}

void
mh_webgl_program_destroy(struct mh_webgl_program *program)
{
	if (program == NULL)
		return;

	glDeleteProgram(program->handle);
	program->handle = 0;
}
