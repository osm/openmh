#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/webgl_pipeline.h"
#include "engine/render/direct3d_pipeline.h"
#include "engine/render/material_pipeline.h"
#include "engine/render/powervr_pipeline.h"

struct profile {
	const char			*name;
	enum mh_error_code (*describe)(enum mh_gpu_pipeline,
		struct mh_raster_state	*, struct mh_error *);
	int				direct3d, powervr;
};

static GLuint program;
static GLint color_uniform, depth_uniform;
static unsigned int failures;

struct fixture {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	context;
	GLuint				framebuffer, color, depth, vertices;
};

static GLuint
shader(GLenum type, const char *source)
{
	GLuint result;
	GLint compiled;
	char message[1024];

	result = glCreateShader(type);
	glShaderSource(result, 1, &source, NULL);
	glCompileShader(result);
	glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);

	if (!compiled) {
		glGetShaderInfoLog(result, sizeof(message), NULL, message);
		fprintf(stderr, "shader: %s\n", message);
		glDeleteShader(result);
		return 0;
	}

	return result;
}

static int
create_program(void)
{
	static const char vertex[] =
		"#version 300 es\n"
		"uniform float depth;\n"
		"void main(){vec2 p=vec2((gl_VertexID==1)?3.0:-1.0,"
		"(gl_VertexID==2)?3.0:-1.0);gl_Position=vec4(p,2.0*depth-1.0,1.0);}";
	static const char fragment[] =
		"#version 300 es\nprecision highp float;\n"
		"uniform vec4 color;out vec4 output_color;"
		"void main(){output_color=color;}";
	GLuint stages[2];
	GLint linked;

	stages[0] = shader(GL_VERTEX_SHADER, vertex);
	stages[1] = shader(GL_FRAGMENT_SHADER, fragment);
	program = glCreateProgram();
	glAttachShader(program, stages[0]);
	glAttachShader(program, stages[1]);
	glLinkProgram(program);
	glDeleteShader(stages[0]);
	glDeleteShader(stages[1]);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	glUseProgram(program);
	color_uniform = glGetUniformLocation(program, "color");
	depth_uniform = glGetUniformLocation(program, "depth");

	return linked && color_uniform >= 0 && depth_uniform >= 0;
}

static void
clear(float red, float green, float blue, float alpha, float depth)
{
	glDepthMask(GL_TRUE);
	glClearColor(red, green, blue, alpha);
	glClearDepthf(depth);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void
draw(const struct profile *profile, enum mh_gpu_pipeline pipeline, float depth,
	float red, float green, float blue, float alpha)
{
	struct mh_raster_state state;

	if (profile->describe(pipeline, &state, NULL) != MH_ERROR_NONE) {
		failures++;
		return;
	}

	mh_webgl_pipeline_apply(&state);
	glUniform4f(color_uniform, red, green, blue, alpha);
	glUniform1f(depth_uniform, depth);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void
pixel(const struct profile *profile, const char *label, float red, float green,
	float blue, float alpha)
{
	unsigned char actual[4];
	float expected[4];
	unsigned int channel;
	int ok;

	expected[0] = red;
	expected[1] = green;
	expected[2] = blue;
	expected[3] = alpha;
	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
	ok = glGetError() == GL_NO_ERROR;

	for (channel = 0; channel < 4; channel++)
		if (fabs(actual[channel] - expected[channel] * 255) > 2)
			ok = 0;

	if (!ok) {
		fprintf(stderr, "%s %s: got %u,%u,%u,%u\n", profile->name,
			label, actual[0], actual[1], actual[2], actual[3]);
		failures++;
	} else
		printf("%s: %s\n", profile->name, label);
}

static void
alpha_depth(const struct profile *profile)
{
	clear(.2f, .4f, .6f, .25f, 1);
	draw(profile, MH_GPU_PIPELINE_ALPHA, .3f, .8f, .2f, .4f, .5f);

	if (profile->powervr)
		pixel(profile, "translucent coverage", .5f, .3f, .5f, .25f);
	else
		pixel(profile, "alpha material replacement", .8f, .2f, .4f,
			.5f);

	draw(profile, MH_GPU_PIPELINE_OPAQUE, .6f, 0, 1, 0, 1);

	if (profile->powervr)
		pixel(profile, "translucency keeps later opaque visible", 0, 1,
			0, 1);
	else
		pixel(profile, "alpha material writes depth", .8f, .2f, .4f,
			.5f);
}

static void
sky_depth_and_blur(const struct profile *profile)
{
	clear(0, 0, 0, 1, .2f);
	draw(profile, MH_GPU_PIPELINE_SKY, .7f, 1, 0, 0, 1);
	draw(profile, MH_GPU_PIPELINE_OPAQUE, .5f, 0, 0, 1, 1);

	if (profile->direct3d)
		pixel(profile, "sky always passes and writes depth", 0, 0, 1,
			1);
	else
		pixel(profile, "sky keeps prior depth", 1, 0, 0, 1);

	clear(.2f, .4f, .6f, .25f, 1);
	glBlendColor(128.0f / 255, 128.0f / 255, 128.0f / 255, 128.0f / 255);
	draw(profile, MH_GPU_PIPELINE_BLUR_SKY, .7f, .8f, .2f, .4f, .25f);

	if (profile->direct3d)
		pixel(profile, "sky blur uses diffuse alpha", .35f, .35f, .55f,
			.25f);
	else
		pixel(profile, "sky blur uses constant half coverage", .501176f,
			.299608f, .499608f, .25f);
}

static void
effects(const struct profile *profile)
{
	clear(.1f, .2f, .3f, .25f, 1);
	draw(profile, MH_GPU_PIPELINE_ADDITIVE, .3f, .3f, .2f, .1f, .5f);
	pixel(profile, "additive RGB and source alpha", .4f, .4f, .4f, .5f);
	draw(profile, MH_GPU_PIPELINE_OPAQUE, .6f, 0, 1, 0, 1);
	pixel(profile, "additive effects do not write depth", 0, 1, 0, 1);

	clear(.2f, .4f, .6f, .25f, 1);
	draw(profile, MH_GPU_PIPELINE_SKID, .3f, .5f, .25f, .75f, .8f);
	pixel(profile, "skids multiply RGB and preserve alpha", .1f, .1f, .45f,
		.25f);
	clear(.2f, .4f, .6f, .25f, 1);
	draw(profile, MH_GPU_PIPELINE_GROUND, .3f, .5f, .25f, .75f, .8f);
	pixel(profile, "ground marks multiply RGB and preserve alpha", .1f, .1f,
		.45f, .25f);

	clear(0, 0, 0, 1, 1);
	draw(profile, MH_GPU_PIPELINE_REFLECTION, .4f, .2f, .3f, .4f, .5f);
	draw(profile, MH_GPU_PIPELINE_PHONG, .4f, .1f, .2f, .1f, .7f);
	draw(profile, MH_GPU_PIPELINE_OPAQUE, .6f, 0, 1, 0, 1);
	pixel(profile, "Phong adds to reflection while retaining depth", .3f,
		.5f, .5f, .5f);
}

static void
fog_and_bias(const struct profile *profile)
{
	clear(.2f, .4f, .6f, .25f, 1);
	draw(profile, MH_GPU_PIPELINE_DISTANT, .3f, .8f, .2f, .4f, .5f);
	pixel(profile, "fog coverage blending", .5f, .3f, .5f, .25f);
	draw(profile, MH_GPU_PIPELINE_OPAQUE, .6f, 0, 1, 0, 1);
	pixel(profile, "distant fog writes depth", .5f, .3f, .5f, .25f);

	clear(0, 0, 1, 1, .5f);
	draw(profile, MH_GPU_PIPELINE_ALPHA, .5000002f, 1, 0, 0, 1);

	if (profile->direct3d)
		pixel(profile, "alpha depth bias reaches the foreground", 1, 0,
			0, 1);
	else
		pixel(profile, "alpha has no depth bias", 0, 0, 1, 1);
}

static int
open_fixture(struct fixture *fixture)
{
	EmscriptenWebGLContextAttributes attributes;

	memset(fixture, 0, sizeof(*fixture));
	emscripten_webgl_init_context_attributes(&attributes);
	attributes.majorVersion = 2;
	attributes.antialias = EM_FALSE;
	fixture->context =
		emscripten_webgl_create_context("#canvas", &attributes);

	if (fixture->context <= 0 ||
		emscripten_webgl_make_context_current(fixture->context) !=
			EMSCRIPTEN_RESULT_SUCCESS)
		return 0;

	glGenFramebuffers(1, &fixture->framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, fixture->framebuffer);
	glGenTextures(1, &fixture->color);
	glBindTexture(GL_TEXTURE_2D, fixture->color);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, fixture->color, 0);
	glGenRenderbuffers(1, &fixture->depth);
	glBindRenderbuffer(GL_RENDERBUFFER, fixture->depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, 4, 4);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, fixture->depth);
	glViewport(0, 0, 4, 4);
	glGenVertexArrays(1, &fixture->vertices);
	glBindVertexArray(fixture->vertices);

	return glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
		GL_FRAMEBUFFER_COMPLETE &&
		create_program();
}

static void
close_fixture(struct fixture *fixture)
{
	if (fixture->context <= 0)
		return;

	glDeleteProgram(program);
	glDeleteVertexArrays(1, &fixture->vertices);
	glDeleteTextures(1, &fixture->color);
	glDeleteRenderbuffers(1, &fixture->depth);
	glDeleteFramebuffers(1, &fixture->framebuffer);
	emscripten_webgl_destroy_context(fixture->context);
}

int	mh_webgl_pipeline_check(void);

int
mh_webgl_pipeline_check(void)
{
	static const struct profile profiles[] = {
		{"Shared material", mh_material_pipeline_state, 0, 0},
		{"Direct3D", mh_direct3d_pipeline_state, 1, 0},
		{"PowerVR", mh_powervr_pipeline_state, 0, 1}};

	struct fixture fixture;
	unsigned int profile;

	failures = 0;

	if (!open_fixture(&fixture))
		failures++;
	else {
		for (profile = 0;
			profile < sizeof(profiles) / sizeof(profiles[0]);
			profile++) {
			alpha_depth(&profiles[profile]);
			sky_depth_and_blur(&profiles[profile]);
			effects(&profiles[profile]);
			fog_and_bias(&profiles[profile]);
		}
	}

	close_fixture(&fixture);

	return failures != 0;
}

int
main(void)
{
	return mh_webgl_pipeline_check();
}
