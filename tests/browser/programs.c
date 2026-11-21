#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/browser/vertices.h"

struct fixture {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	context;
	GLuint				texture, blocks[3];
	struct mh_test_webgl_vertices	vertices;
	struct mh_webgl_program		programs[MH_WEBGL_PROGRAM_COUNT];
};

static const char *names[] = {"Material", "Material Glenz", "Direct3D",
	"Direct3D Glenz", "PowerVR", "PowerVR Glenz", "UI", "TRON", "Retro",
	"Depth", "OpenMH"};
static unsigned int failures;

static void
check(int success, const char *name, const char *scenario)
{
	if (success)
		printf("%s: %s\n", name, scenario);
	else {
		fprintf(stderr, "%s: %s failed\n", name, scenario);
		failures++;
	}
}

static void
pixel(enum mh_webgl_program_kind kind, const char *scenario, int red, int green,
	int blue)
{
	unsigned char actual[4];
	int success;

	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
	success = glGetError() == GL_NO_ERROR && abs(actual[0] - red) <= 1 &&
		abs(actual[1] - green) <= 1 && abs(actual[2] - blue) <= 1;

	if (!success)
		fprintf(stderr, "got %u,%u,%u; expected %d,%d,%d\n", actual[0],
			actual[1], actual[2], red, green, blue);

	check(success, names[kind], scenario);
}

static void
block(struct fixture *fixture, unsigned int slot, const float *data,
	size_t count)
{
	glBindBuffer(GL_UNIFORM_BUFFER, fixture->blocks[slot]);

	glBufferData(
		GL_UNIFORM_BUFFER, count * sizeof(float), data, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, slot, fixture->blocks[slot]);
}

static void
draw(struct fixture *fixture, enum mh_webgl_program_kind kind)
{
	glUseProgram(fixture->programs[kind].handle);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void
scene(struct fixture *fixture, enum mh_webgl_program_kind kind)
{
	float camera[60], material[8];
	float triangle[] = {-1, -1, .4f, 3, -1, .4f, -1, 3, .4f};
	unsigned int base, diagonal;
	int glenz, powervr;

	glenz = kind == MH_WEBGL_MATERIAL_GLENZ ||
		kind == MH_WEBGL_DIRECT3D_GLENZ ||
		kind == MH_WEBGL_POWERVR_GLENZ;
	powervr = kind == MH_WEBGL_POWERVR || kind == MH_WEBGL_POWERVR_GLENZ;
	memset(camera, 0, sizeof(camera));
	memset(material, 0, sizeof(material));

	for (base = 0; base < 48; base += 16)
		for (diagonal = 0; diagonal < 16; diagonal += 5)
			camera[base + diagonal] = 1;

	camera[26] = -1;
	camera[48] = 1;
	camera[49] = camera[50] = 4;
	camera[51] = (float)powervr;
	camera[55] = 1;
	material[0] = glenz ? 1.0f : 0.0f;
	material[1] = material[7] = 1;
	block(fixture, 0, camera, 60);
	block(fixture, 1, material, 8);
	check(mh_test_webgl_vertices_scene(
		&fixture->vertices, kind, triangle, powervr),
		names[kind], "vertex array preserves bindings");
	draw(fixture, kind);
	pixel(kind, "camera, material and texture bindings", glenz ? 0 : 255,
		glenz ? 255 : 0, 0);

	if (glenz) {
		material[0] = 2;
		block(fixture, 1, material, 8);
		draw(fixture, kind);
		pixel(kind, "Phong selects its own coordinates", 0, 0, 255);
	} else {
		mh_test_webgl_vertices_depth(&fixture->vertices, -.2f);
		draw(fixture, kind);
		pixel(kind,
			"buffer update retains layout and clips negative depth",
			0, 0, 0);
	}
}

static void
overlays(struct fixture *fixture)
{
	static const float viewport[] = {4, 4, 0, 0};
	static const float poison[] = {1, 1, 1, 1, 1, 1, 1, 1};
	float material[4];

	block(fixture, 0, viewport, 4);
	block(fixture, 1, poison, 8);
	check(mh_test_webgl_vertices_overlay(&fixture->vertices, MH_WEBGL_UI),
		names[MH_WEBGL_UI], "vertex array preserves bindings");
	draw(fixture, MH_WEBGL_UI);
	pixel(MH_WEBGL_UI, "viewport and atlas bindings", 0, 128, 0);

	memset(material, 0, sizeof(material));
	block(fixture, 2, material, 4);
	check(mh_test_webgl_vertices_overlay(
		&fixture->vertices, MH_WEBGL_RETRO),
		names[MH_WEBGL_RETRO], "vertex array preserves bindings");
	draw(fixture, MH_WEBGL_RETRO);
	pixel(MH_WEBGL_RETRO, "untextured fragment material", 64, 128, 255);
	material[0] = 1;
	block(fixture, 2, material, 4);
	draw(fixture, MH_WEBGL_RETRO);
	pixel(MH_WEBGL_RETRO, "textured fragment material", 0, 128, 0);

	material[0] = 0;
	block(fixture, 2, material, 4);
	check(mh_test_webgl_vertices_overlay(&fixture->vertices, MH_WEBGL_TRON),
		names[MH_WEBGL_TRON], "vertex array preserves bindings");
	draw(fixture, MH_WEBGL_TRON);
	pixel(MH_WEBGL_TRON, "unfogged fragment material", 255, 128, 64);
	material[0] = 1;
	block(fixture, 2, material, 4);
	draw(fixture, MH_WEBGL_TRON);
	pixel(MH_WEBGL_TRON, "fog uses fragment material slot", 127, 64, 32);
}

static int
open_fixture(struct fixture *fixture)
{
	static const unsigned char texels[] = {255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255};

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

	glViewport(0, 0, 4, 4);
	glDisable(GL_DITHER);
	mh_test_webgl_vertices_init(&fixture->vertices);
	glGenBuffers(3, fixture->blocks);
	glGenTextures(1, &fixture->texture);
	glBindTexture(GL_TEXTURE_2D, fixture->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, texels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	return glGetError() == GL_NO_ERROR;
}

static void
close_fixture(struct fixture *fixture)
{
	unsigned int i;

	if (fixture->context <= 0)
		return;

	glUseProgram(0);

	for (i = 0; i < MH_WEBGL_PROGRAM_COUNT; i++)
		mh_webgl_program_destroy(&fixture->programs[i]);

	glDeleteBuffers(3, fixture->blocks);
	mh_test_webgl_vertices_free(&fixture->vertices);
	glDeleteTextures(1, &fixture->texture);
	emscripten_webgl_destroy_context(fixture->context);
}

static int
create_programs(struct fixture *fixture)
{
	struct mh_error error;
	enum mh_webgl_program_kind kind;
	GLint current;
	GLuint previous;

	previous = 0;

	for (kind = MH_WEBGL_MATERIAL; kind < MH_WEBGL_PROGRAM_COUNT; kind++) {
		if (mh_webgl_program_create(&fixture->programs[kind], kind,
			&error) != MH_ERROR_NONE) {
			fprintf(stderr, "%s: %s\n", names[kind], error.message);
			return 0;
		}

		glGetIntegerv(GL_CURRENT_PROGRAM, &current);
		check((GLuint)current == previous, names[kind],
			"creation preserves active program");
		glUseProgram(fixture->programs[kind].handle);
		previous = fixture->programs[kind].handle;
	}

	return 1;
}

static void
lifetime(struct fixture *fixture)
{
	struct mh_webgl_program *program;
	GLuint handle;

	program = &fixture->programs[MH_WEBGL_UI];
	handle = program->handle;
	check(mh_webgl_program_create(program, MH_WEBGL_UI, NULL) ==
				MH_ERROR_STATE &&
			program->handle == handle && glIsProgram(handle),
		"lifetime", "live program cannot be overwritten");
	glUseProgram(0);
	mh_webgl_program_destroy(program);
	mh_webgl_program_destroy(program);
	check(program->handle == 0 && !glIsProgram(handle), "lifetime",
		"repeated destruction releases program");
	check(mh_webgl_program_create(program, MH_WEBGL_PROGRAM_COUNT, NULL) ==
				MH_ERROR_INVALID &&
			program->handle == 0,
		"lifetime", "invalid kind creates no program");
	check(mh_webgl_program_create(program, MH_WEBGL_UI, NULL) ==
			MH_ERROR_NONE,
		"lifetime", "destroyed program can be recreated");
}

int	mh_webgl_program_check(void);

int
mh_webgl_program_check(void)
{
	struct fixture fixture;
	enum mh_webgl_program_kind kind;

	failures = 0;

	if (!open_fixture(&fixture) || !create_programs(&fixture))
		failures++;
	else {
		for (kind = MH_WEBGL_MATERIAL; kind <= MH_WEBGL_POWERVR_GLENZ;
			kind++)
			scene(&fixture, kind);

		overlays(&fixture);
		lifetime(&fixture);
		check(mh_test_webgl_vertices_lifetime(&fixture.vertices),
			"vertex arrays", "ownership and invalid input");
		check(glGetError() == GL_NO_ERROR, "programs", "no API errors");
	}

	close_fixture(&fixture);

	return failures != 0;
}

int
main(void)
{
	return mh_webgl_program_check();
}
