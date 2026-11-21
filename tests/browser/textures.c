#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_sampler.h"
#include "backends/render/webgl_target.h"
#include "engine/render/powervr_texture.h"

struct fixture {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	context;
	struct mh_webgl_target		target;
	GLuint				program, vertices, samplers[4];
	GLint				uv, lod, depth;
};

static unsigned int failures;

static void
check(int success, const char *label)
{
	if (success)
		printf("%s\n", label);
	else {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	}
}

static int
operation(enum mh_error_code status, const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error->message);
	failures++;

	return 0;
}

static GLuint
shader(GLenum stage, const char *source)
{
	GLuint handle;
	GLint compiled;
	char log[1024];

	handle = glCreateShader(stage);
	glShaderSource(handle, 1, &source, NULL);
	glCompileShader(handle);
	glGetShaderiv(handle, GL_COMPILE_STATUS, &compiled);

	if (!compiled) {
		glGetShaderInfoLog(handle, sizeof(log), NULL, log);
		fprintf(stderr, "%s\n", log);
		glDeleteShader(handle);
		return 0;
	}

	return handle;
}

static int
create_program(struct fixture *fixture)
{
	static const char vertex[] =
		"#version 300 es\n"
		"void main(){gl_Position=vec4(gl_VertexID==1?3.0:-1.0,"
		"gl_VertexID==2?3.0:-1.0,0.0,1.0);}";
	static const char fragment[] =
		"#version 300 es\nprecision highp float;\n"
		"uniform sampler2D source;uniform vec2 uv;uniform float lod,depth;"
		"out vec4 color;void main(){color=textureLod(source,uv,lod);gl_FragDepth=depth;}";
	GLuint stages[2];
	GLint linked;

	stages[0] = shader(GL_VERTEX_SHADER, vertex);
	stages[1] = shader(GL_FRAGMENT_SHADER, fragment);
	fixture->program = glCreateProgram();
	glAttachShader(fixture->program, stages[0]);
	glAttachShader(fixture->program, stages[1]);
	glLinkProgram(fixture->program);
	glDeleteShader(stages[0]);
	glDeleteShader(stages[1]);
	glGetProgramiv(fixture->program, GL_LINK_STATUS, &linked);
	glUseProgram(fixture->program);
	fixture->uv = glGetUniformLocation(fixture->program, "uv");
	fixture->lod = glGetUniformLocation(fixture->program, "lod");
	fixture->depth = glGetUniformLocation(fixture->program, "depth");

	return linked && fixture->uv >= 0 && fixture->lod >= 0 &&
		fixture->depth >= 0;
}

static void
pixel(const char *label, int red, int green, int blue, int tolerance)
{
	unsigned char actual[4];
	int success;

	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
	success = glGetError() == GL_NO_ERROR &&
		abs(actual[0] - red) <= tolerance &&
		abs(actual[1] - green) <= tolerance &&
		abs(actual[2] - blue) <= tolerance;

	if (!success)
		fprintf(stderr, "%s: got %u,%u,%u expected %d,%d,%d\n", label,
			actual[0], actual[1], actual[2], red, green, blue);

	check(success, label);
}

static void
alpha(const char *label, int expected)
{
	unsigned char actual[4];

	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
	check(glGetError() == GL_NO_ERROR && actual[3] == expected, label);
}

static void
sample(const struct fixture *fixture, const struct mh_webgl_texture *texture,
	unsigned int sampler, float u, float v, float lod, float depth)
{
	glBindTexture(GL_TEXTURE_2D, texture->handle);

	glBindSampler(0, fixture->samplers[sampler]);
	glUniform2f(fixture->uv, u, v);
	glUniform1f(fixture->lod, lod);
	glUniform1f(fixture->depth, depth);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static int
upload_with_state(const struct fixture *fixture, const struct mh_image *image,
	struct mh_webgl_texture *texture)
{
	struct mh_error error;
	GLuint unpack;
	GLint actual;
	int restored, success;

	glGenBuffers(1, &unpack);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, NULL, GL_STREAM_DRAW);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 9);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
	glBindTexture(GL_TEXTURE_2D, fixture->target.color.handle);

	success = operation(
		mh_webgl_texture_from_image(texture, image, 0, 1, &error),
		&error);

	glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual);
	restored = (GLuint)actual == fixture->target.color.handle;
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual);
	restored = restored && (GLuint)actual == unpack;
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &actual);
	restored = restored && actual == 8;
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &actual);
	restored = restored && actual == 9;
	glGetIntegerv(GL_UNPACK_SKIP_ROWS, &actual);
	restored = restored && actual == 1;
	glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &actual);
	check(restored && actual == 2,
		"upload restores texture and pixel-unpack bindings");
	glDeleteBuffers(1, &unpack);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

	return success;
}

static void
filter_pixels(
	const struct fixture *fixture, const struct mh_webgl_texture *texture)
{
	sample(fixture, texture, 0, .25f, .25f, 0, .5f);

	pixel("uploaded top-left texel", 255, 0, 0, 0);
	sample(fixture, texture, 0, .25f, .75f, 0, .5f);
	pixel("uploaded bottom-left texel", 0, 0, 255, 0);
	sample(fixture, texture, 0, -.25f, -.25f, 0, .5f);
	pixel("nearest clamps both axes", 255, 0, 0, 0);
	sample(fixture, texture, 1, .5f, .5f, 0, .5f);
	pixel("bilinear averages four texels", 128, 128, 128, 1);
	sample(fixture, texture, 3, -.25f, -.25f, 0, .5f);
	pixel("sky wrapping repeats U and clamps V", 0, 255, 0, 0);
}

static void
depth_pixels(
	const struct fixture *fixture, const struct mh_webgl_texture *texture)
{
	glEnable(GL_DEPTH_TEST);

	glDepthFunc(GL_LEQUAL);
	glClearDepthf(1);
	glClear(GL_DEPTH_BUFFER_BIT);
	sample(fixture, texture, 0, .25f, .25f, 0, .25f);
	sample(fixture, texture, 0, .25f, .75f, 0, .75f);
	pixel("D32F render target rejects later distant fragments", 255, 0, 0,
		0);
	glDisable(GL_DEPTH_TEST);
}

static void
filtering(const struct fixture *fixture)
{
	unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
		255, 255, 255, 255, 255};

	struct mh_image image;
	struct mh_webgl_texture texture;

	memset(&texture, 0, sizeof(texture));
	image.width = image.height = 2;
	image.rgba = pixels;

	if (upload_with_state(fixture, &image, &texture)) {
		filter_pixels(fixture, &texture);
		depth_pixels(fixture, &texture);
	}

	mh_webgl_texture_destroy(&texture);
}

static void
mips(const struct fixture *fixture, unsigned int format)
{
	struct mh_image image;
	struct mh_webgl_texture texture;
	struct mh_error error;
	unsigned char pixels[64];
	unsigned int i;
	int packed, indexed;

	packed = format == MH_TEXTURE_RGB565;
	indexed = format == MH_TEXTURE_PALETTE_RGB;
	memset(&texture, 0, sizeof(texture));
	memset(pixels, 0, sizeof(pixels));

	for (i = 0; i < 16; i++) {
		pixels[i * 4] = packed ? (unsigned char)((i << 3) | (i >> 2))
				: (unsigned char)(i * 16);

		pixels[i * 4 + 1] = packed ? (unsigned char)(i << 2) : 0;
		pixels[i * 4 + 2] = packed ? pixels[i * 4] : 0;
		pixels[i * 4 + 3] = format == 0 ? (unsigned char)(i * 16) : 255;
	}

	image.width = image.height = 4;
	image.rgba = pixels;

	if (!operation(mh_webgl_texture_from_image(
			&texture, &image, format, 3, &error),
		&error))
		return;

	sample(fixture, &texture, 2, .25f, .25f, 1, .5f);

	if (packed)
		pixel("RGB565 half-size packed average", 16, 8, 16, 0);
	else if (indexed)
		pixel("indexed half-size selection", 0, 0, 0, 0);
	else
		pixel("true-color half-size base average", 40, 0, 0, 0);

	if (format == 0)
		alpha("half-size mip retains averaged alpha", 40);

	sample(fixture, &texture, 2, .5f, .5f, 2, .5f);

	if (packed)
		pixel("RGB565 quarter-size packed average", 57, 28, 57, 0);
	else if (indexed)
		pixel("indexed quarter-size selection", 0, 0, 0, 0);
	else
		pixel("true-color quarter-size base average", 120, 0, 0, 0);

	if (format == 0)
		alpha("quarter-size mip retains averaged alpha", 120);

	if (format == 0) {
		sample(fixture, &texture, 2, .125f, .125f, .5f, .5f);
		pixel("trilinear blends adjacent authored mip levels", 20, 0, 0,
			1);
		sample(fixture, &texture, 1, .125f, .125f, 2, .5f);
		pixel("bilinear remains on base level", 0, 0, 0, 0);
		sample(fixture, &texture, 0, .125f, .125f, 2, .5f);
		pixel("nearest remains on base level", 0, 0, 0, 0);
	}

	mh_webgl_texture_destroy(&texture);
}

static void
powervr(const struct fixture *fixture, enum mh_texture_format format)
{
	unsigned char source_pixels[32 * 4] = {123, 93, 45, 127};
	unsigned char prepared_pixels[32 * 32 * 4];
	struct mh_image source, prepared;
	struct mh_webgl_texture texture;
	struct mh_error error;

	memset(&texture, 0, sizeof(texture));
	source.width = 32;
	source.height = 1;
	source.rgba = source_pixels;
	prepared.width = prepared.height = 32;
	prepared.rgba = prepared_pixels;

	if (!operation(mh_powervr_texture_prepare(
			&source, format, &prepared, &error),
		&error) ||
		!operation(mh_webgl_texture_from_image(
				&texture, &prepared, format, 1, &error),
			&error))
		return;

	sample(fixture, &texture, 0, .5f / 32, .5f / 32, 0, .5f);

	if (format == MH_TEXTURE_RGB555)
		pixel("PowerVR RGB555 upload precision", 123, 90, 41, 0);
	else {
		pixel("PowerVR ARGB4444 upload precision", 119, 85, 34, 0);
		alpha("PowerVR ARGB4444 retains texture alpha", 119);
	}

	sample(fixture, &texture, 0, .75f, .75f, 0, .5f);
	pixel("PowerVR square padding remains black", 0, 0, 0, 0);
	alpha("PowerVR padding alpha follows source format",
		format == MH_TEXTURE_RGB555 ? 255 : 0);
	mh_webgl_texture_destroy(&texture);
}

static void
targets(const struct fixture *fixture)
{
	struct mh_webgl_target first, second;
	struct mh_error error;
	GLint read, draw;
	GLuint framebuffer, color;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));

	if (!operation(mh_webgl_target_create(&first, 4, 4, 0, &error), &error))
		return;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, first.framebuffer);

	if (operation(
		mh_webgl_target_create(&second, 4, 4, 0, &error), &error)) {
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
		check((GLuint)read == first.framebuffer &&
				(GLuint)draw == fixture->target.framebuffer,
			"target creation preserves separate read and draw bindings");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, first.framebuffer);
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 4, 2);
	glClearColor(0, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glScissor(0, 2, 4, 2);
	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, fixture->target.framebuffer);
	sample(fixture, &first.color, 0, .25f, .25f, 0, .5f);
	pixel("render-target texture starts at framebuffer bottom", 0, 0, 255,
		0);
	sample(fixture, &first.color, 0, .25f, .75f, 0, .5f);
	pixel("render-target texture retains framebuffer top", 255, 0, 0, 0);
	framebuffer = first.framebuffer;
	color = first.color.handle;
	mh_webgl_target_destroy(&first);
	mh_webgl_target_destroy(&first);
	check(!glIsFramebuffer(framebuffer) && !glIsTexture(color),
		"target owns and releases its attachments");
	mh_webgl_target_destroy(&second);
}

static void
invalid_requests(const struct fixture *fixture)
{
	struct mh_webgl_texture texture;
	struct mh_image image;
	unsigned char pixel[4] = {0, 0, 0, 255};
	GLuint sampler;

	memset(&texture, 0, sizeof(texture));
	image.width = image.height = 1;
	image.rgba = pixel;
	check(mh_webgl_texture_from_image(&texture, &image, 0, 2, NULL) ==
				MH_ERROR_INVALID &&
			texture.handle == 0,
		"impossible mip chain leaves output empty");
	check(mh_webgl_texture_create(&texture, UINT_MAX, 1, 1, GL_RGBA8,
		NULL) == MH_ERROR_RANGE &&
			texture.handle == 0,
		"oversized texture is rejected before allocation");
	check(mh_webgl_texture_upload(&fixture->target.depth, 0, &image,
		NULL) == MH_ERROR_INVALID,
		"RGBA upload to a depth texture is rejected");
	check(mh_webgl_texture_upload(&fixture->target.color, 0, &image,
		NULL) == MH_ERROR_INVALID,
		"mismatched upload dimensions are rejected");
	sampler = 0;
	check(mh_webgl_sampler_create(&sampler, MH_TEXTURE_FILTER_ORIGINAL, 0,
		0, NULL) == MH_ERROR_INVALID &&
			sampler == 0,
		"unresolved profile filter is rejected");
}

static int
open_fixture(struct fixture *fixture)
{
	EmscriptenWebGLContextAttributes attributes;
	struct mh_error error;
	unsigned int i;

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

	if (!operation(
		mh_webgl_target_create(&fixture->target, 4, 4, 1, &error),
		&error))
		return 0;

	glBindFramebuffer(GL_FRAMEBUFFER, fixture->target.framebuffer);
	glViewport(0, 0, 4, 4);
	glDisable(GL_DITHER);
	glGenVertexArrays(1, &fixture->vertices);
	glBindVertexArray(fixture->vertices);

	for (i = 0; i < 3; i++)
		if (!operation(mh_webgl_sampler_create(&fixture->samplers[i],
				(enum mh_texture_filter)(
					MH_TEXTURE_FILTER_NEAREST + i),
				0, 0, &error),
			&error))
			return 0;

	if (!operation(mh_webgl_sampler_create(&fixture->samplers[3],
			MH_TEXTURE_FILTER_NEAREST, 1, 0, &error),
		&error))
		return 0;

	return create_program(fixture);
}

static void
close_fixture(struct fixture *fixture)
{
	if (fixture->context <= 0)
		return;

	glDeleteProgram(fixture->program);
	glDeleteVertexArrays(1, &fixture->vertices);
	glDeleteSamplers(4, fixture->samplers);
	mh_webgl_target_destroy(&fixture->target);
	emscripten_webgl_destroy_context(fixture->context);
}

int	mh_webgl_texture_check(void);

int
mh_webgl_texture_check(void)
{
	struct fixture fixture;

	failures = 0;

	if (!open_fixture(&fixture))
		failures++;
	else {
		filtering(&fixture);
		mips(&fixture, 0);
		mips(&fixture, MH_TEXTURE_PALETTE_RGB);
		mips(&fixture, MH_TEXTURE_RGB565);
		mips(&fixture, MH_TEXTURE_XRGB8888);
		powervr(&fixture, MH_TEXTURE_RGB555);
		powervr(&fixture, MH_TEXTURE_ARGB4444);
		targets(&fixture);
		invalid_requests(&fixture);
		check(glGetError() == GL_NO_ERROR,
			"texture fixtures leave no API errors");
	}

	close_fixture(&fixture);

	return failures != 0;
}

int
main(void)
{
	return mh_webgl_texture_check();
}
