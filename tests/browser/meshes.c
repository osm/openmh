#include <emscripten/html5.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/webgl_mesh.h"
#include "backends/render/webgl_program.h"

struct fixture {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	context;
	struct mh_webgl_program		ordinary, glenz, powervr;
	GLuint				blocks[2];
	struct mh_mesh			source[2];
	struct mh_draw			draws[2];
	struct mh_image			images[2];
	struct mh_vertex		vertices[6];
	float				atlas_uv[12], raw[18];
	unsigned char			white[4], pixels[64];
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

static void
pixel(unsigned int x, unsigned int y, unsigned int r, unsigned int g,
	unsigned int b, const char *label)
{
	unsigned char rgba[4];

	glReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	check(rgba[0] == r && rgba[1] == g && rgba[2] == b, label);
}

static void
material(struct fixture *fixture, int flat, int glenz)
{
	float parameters[8];

	memset(parameters, 0, sizeof(parameters));
	parameters[0] = (float)glenz;
	parameters[1] = 1;
	parameters[6] = (float)flat;
	glBindBuffer(GL_UNIFORM_BUFFER, fixture->blocks[1]);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(parameters), parameters,
		GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, fixture->blocks[1]);
	glUseProgram(glenz ? fixture->glenz.handle : fixture->ordinary.handle);
}

static void
draw(struct mh_webgl_mesh *mesh, size_t texture)
{
	struct mh_error error;

	if (!operation(mh_webgl_meshes_upload(mesh, 1, &error), &error))
		return;

	glClear(GL_COLOR_BUFFER_BIT);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D,
		mesh->pool->items[mesh->images[texture]].texture.handle);
	operation(
		mh_webgl_geometry_draw(&mesh->geometry, 0, 6, &error), &error);
}

static void
source_meshes(struct fixture *fixture)
{
	static const float xy[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

	unsigned int i, x, y;

	memset(fixture->white, 255, sizeof(fixture->white));

	for (y = 0; y < 4; y++)
		for (x = 0; x < 4; x++) {
			i = (y * 4 + x) * 4;
			fixture->pixels[i] =
				(x < 2 && y < 2) || (x >= 2 && y >= 2) ? 255
								: 0;
			fixture->pixels[i + 1] = x >= 2 ? 255 : 0;
			fixture->pixels[i + 2] = y >= 2 ? 255 : 0;
			fixture->pixels[i + 3] = 255;
		}

	fixture->images[0].width = fixture->images[0].height = 1;
	fixture->images[0].rgba = fixture->white;
	fixture->images[1].width = fixture->images[1].height = 4;
	fixture->images[1].rgba = fixture->pixels;

	for (i = 0; i < 6; i++) {
		fixture->vertices[i].position[0] = xy[i][0];
		fixture->vertices[i].position[1] = xy[i][1];
		fixture->vertices[i].position[2] = 0.5f;
		fixture->vertices[i].uv[0] = fixture->vertices[i].uv[1] = 0.25f;
		fixture->vertices[i].color[i == 0 ? 0 : i == 3 ? 2 : 1] = 1;
		fixture->atlas_uv[i * 2] = 0.75f;
		fixture->atlas_uv[i * 2 + 1] = 0.25f;
		fixture->raw[i * 3] = 128;
	}

	for (i = 0; i < 2; i++) {
		fixture->draws[i].count = 6;
		fixture->draws[i].texture = 1;
		fixture->draws[i].environment = i == 0;
		fixture->source[i].vertices = fixture->vertices;
		fixture->source[i].vertex_count = 6;
		fixture->source[i].draws = &fixture->draws[i];
		fixture->source[i].draw_count = 1;
		fixture->source[i].textures = fixture->images;
		fixture->source[i].texture_count = 2;
	}
}

static int
open_fixture(struct fixture *fixture)
{
	EmscriptenWebGLContextAttributes attributes;
	struct mh_error error;
	float camera[60];
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

	source_meshes(fixture);
	glViewport(0, 0, 4, 4);
	glDisable(GL_DITHER);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 1);
	glGenBuffers(2, fixture->blocks);
	memset(camera, 0, sizeof(camera));

	for (i = 0; i < 3; i++)
		camera[i * 16] = camera[i * 16 + 5] = camera[i * 16 + 10] =
			camera[i * 16 + 15] = 1;

	camera[48] = 1;
	camera[49] = camera[50] = 4;
	camera[55] = 1;
	glBindBuffer(GL_UNIFORM_BUFFER, fixture->blocks[0]);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(camera), camera, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, fixture->blocks[0]);

	return operation(mh_webgl_program_create(
				&fixture->ordinary, MH_WEBGL_MATERIAL, &error),
		&error) &&
		operation(mh_webgl_program_create(&fixture->glenz,
				MH_WEBGL_MATERIAL_GLENZ, &error),
			&error) &&
		operation(mh_webgl_program_create(
				&fixture->powervr, MH_WEBGL_POWERVR, &error),
			&error);
}

static void
geometry_bindings(void)
{
	struct mh_webgl_geometry geometry;
	struct mh_error error;
	GLuint array, buffers[3];
	GLint binding;

	memset(&geometry, 0, sizeof(geometry));
	glGenVertexArrays(1, &array);
	glBindVertexArray(array);
	glGenBuffers(3, buffers);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[0]);
	glBindBuffer(GL_ARRAY_BUFFER, buffers[1]);
	glBindBuffer(GL_COPY_WRITE_BUFFER, buffers[2]);

	if (operation(mh_webgl_geometry_create(&geometry, 6, &error), &error)) {
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &binding);
		check((GLuint)binding == array,
			"geometry creation retains the caller vertex array");
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
		check((GLuint)binding == buffers[0],
			"geometry creation retains the caller element buffer");
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
		check((GLuint)binding == buffers[1],
			"geometry creation retains the caller vertex buffer");
		glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &binding);
		check((GLuint)binding == buffers[2],
			"geometry creation retains the caller copy buffer");
	}

	mh_webgl_geometry_destroy(&geometry);
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &array);
	glDeleteBuffers(3, buffers);
}

static void
dynamic_attributes(struct fixture *fixture, struct mh_webgl_mesh *mesh)
{
	struct mh_vertex lit[6];
	struct mh_error error;
	unsigned int i;

	memcpy(lit, fixture->vertices, sizeof(lit));

	for (i = 0; i < 6; i++) {
		lit[i].color[0] = lit[i].color[1] = lit[i].color[2] = 1;
		lit[i].projection_w = 1;
	}

	fixture->source[0].atlas_uv = fixture->atlas_uv;
	fixture->source[0].material_rgb = fixture->raw;
	fixture->source[0].material_rgb_count = 6;

	if (!operation(mh_webgl_mesh_update(
			mesh, lit, MH_SCENE_EFFECT_OPAQUE, 1, &error),
		&error))
		return;

	material(fixture, 0, 0);
	draw(mesh, 1);
	pixel(1, 1, 0, 255, 0,
		"dynamic upload uses atlas UVs instead of source UVs");
	check(mesh->lit && mesh->colors[0] == 1 && mesh->projection_w[0] == 1 &&
			mesh->material_rgb[0] == 128,
		"successful upload publishes CPU lighting attributes");
	check(lit[0].uv[0] == 0.25f && fixture->vertices[0].color[1] == 0,
		"upload leaves source vertices unchanged");
	fixture->source[0].material_rgb_count = 5;
	check(mh_webgl_mesh_update(mesh, NULL, MH_SCENE_EFFECT_SKIDS, 0,
		&error) == MH_ERROR_INVALID &&
			mesh->effect == MH_SCENE_EFFECT_OPAQUE && mesh->lit &&
			mesh->colors[0] == 1,
		"rejected mesh update retains CPU attributes and effect");
	draw(mesh, 1);
	pixel(1, 1, 0, 255, 0, "rejected mesh update retains GPU vertices");
	fixture->source[0].material_rgb_count = 6;
}

static void
reflection_updates(struct fixture *fixture, struct mh_webgl_mesh *mesh)
{
	struct mh_glenz_coordinates coordinates[6];
	struct mh_renderer_glenz glenz;
	struct mh_error error;
	unsigned int i;

	for (i = 0; i < 6; i++) {
		coordinates[i].reflection[0] = 0.75f;
		coordinates[i].reflection[1] = 0.25f;
		coordinates[i].phong[0] = 0.25f;
		coordinates[i].phong[1] = 0.75f;
	}

	glenz.coordinates = coordinates;
	glenz.count = 6;
	glenz.reflection_texture = glenz.phong_texture = 1;

	if (!operation(mh_webgl_mesh_glenz(mesh, &glenz, &error), &error))
		return;

	check(mesh->glenz && mesh->glenz_reflection == 1 &&
			mesh->glenz_coordinates[0].phong[1] == 0.75f,
		"Glenz upload publishes independent coordinates and map selection");
	material(fixture, 0, 1);
	draw(mesh, mesh->glenz_reflection);
	pixel(1, 1, 0, 255, 0,
		"mesh reflection buffer reaches the production shader");
	material(fixture, 0, 2);
	draw(mesh, mesh->glenz_phong);
	pixel(1, 1, 0, 0, 255,
		"mesh Phong buffer reaches the production shader");
	coordinates[0].phong[0] = -1;
	check(mh_webgl_mesh_glenz(mesh, &glenz, &error) == MH_ERROR_INVALID &&
			mesh->glenz_coordinates[0].phong[0] == 0.25f,
		"invalid Glenz update retains CPU coordinates");
	draw(mesh, 1);
	pixel(1, 1, 0, 0, 255, "invalid Glenz update retains GPU coordinates");
	check(mh_webgl_mesh_glenz(mesh, NULL, &error) == MH_ERROR_NONE &&
			!mesh->glenz,
		"Glenz can be disabled without replacing mesh vertices");
}

static void
batched_uploads(struct fixture *fixture)
{
	const struct mh_mesh *sources[2];
	struct mh_webgl_images pool;
	struct mh_webgl_mesh meshes[2];
	struct mh_vertex vertices[6];
	struct mh_error error;
	GLint binding;
	unsigned int i;

	memset(&pool, 0, sizeof(pool));
	memset(meshes, 0, sizeof(meshes));
	sources[0] = &fixture->source[0];
	sources[1] = &fixture->source[1];

	if (!operation(mh_webgl_images_create(&pool, sources, 2, 0, &error),
		&error))
		return;

	for (i = 0; i < 2; i++)
		if (!operation(mh_webgl_mesh_create(&meshes[i], sources[i],
				&pool, i, &error),
			&error))
			goto cleanup;

	memcpy(vertices, fixture->vertices, sizeof(vertices));

	for (i = 0; i < 6; i++) {
		vertices[i].color[0] = vertices[i].color[2] = 0;
		vertices[i].color[1] = 1;
	}

	for (i = 0; i < 2; i++)
		if (!operation(mh_webgl_mesh_update(&meshes[i], vertices,
				MH_SCENE_EFFECT_OPAQUE, 1, &error),
			&error))
			goto cleanup;

	/* Staging changes CPU attributes without changing GPU contents. */
	material(fixture, 1, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(
		GL_TEXTURE_2D, pool.items[pool.bindings[0][0]].texture.handle);
	operation(mh_webgl_geometry_draw(&meshes[0].geometry, 0, 6, &error),
		&error);
	pixel(3, 0, 255, 0, 0, "staged lighting waits for the upload batch");

	/*
	 * Force a driver error in the second upload, after the first succeeds.
	 */
	glBindBuffer(GL_ARRAY_BUFFER, meshes[1].geometry.vertices);
	glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
	check(mh_webgl_meshes_upload(meshes, 2, &error) == MH_ERROR_BACKEND &&
			meshes[0].vertices_pending &&
			meshes[1].vertices_pending,
		"partial GPU failure retains the complete pending upload batch");
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
	check((GLuint)binding == meshes[1].geometry.vertices,
		"failed upload batch restores the previous buffer binding");
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_DYNAMIC_DRAW);

	if (!operation(mh_webgl_meshes_upload(meshes, 2, &error), &error))
		goto cleanup;

	check(!meshes[0].vertices_pending && !meshes[1].vertices_pending,
		"successful retry commits every pending upload");

	for (i = 0; i < 2; i++) {
		draw(&meshes[i], 0);
		pixel(3, 0, 0, 255, 0,
			"retried batch draws the staged lighting");
	}

cleanup:
	for (i = 0; i < 2; i++)
		mh_webgl_mesh_destroy(&meshes[i]);

	mh_webgl_images_destroy(&pool);
}

static void
ordinary_meshes(struct fixture *fixture)
{
	const struct mh_mesh *sources[2];
	struct mh_webgl_images pool;
	struct mh_webgl_mesh mesh;
	struct mh_error error;
	GLuint vertices, array, texture;

	memset(&pool, 0, sizeof(pool));
	memset(&mesh, 0, sizeof(mesh));
	sources[0] = &fixture->source[0];
	sources[1] = &fixture->source[1];

	if (!operation(mh_webgl_images_create(&pool, sources, 2, 0, &error),
		&error))
		return;

	check(pool.count == 2 && pool.bindings[0][1] == pool.bindings[1][1],
		"image pool shares pixel storage across meshes");
	check(pool.items[pool.bindings[0][1]].texture.levels == 3,
		"later ordinary draws add mips to a shared environment image");

	if (!operation(
		mh_webgl_mesh_create(&mesh, sources[0], &pool, 0, &error),
		&error))
		goto cleanup;

	material(fixture, 1, 0);
	draw(&mesh, 0);
	pixel(3, 0, 255, 0, 0,
		"first source corner controls flat color in the first triangle");
	pixel(0, 3, 0, 0, 255,
		"first source corner controls flat color in the second triangle");
	check(mh_webgl_geometry_draw(&mesh.geometry, 1, 3, &error) ==
				MH_ERROR_INVALID &&
			mh_webgl_geometry_draw(&mesh.geometry, 3, 6, &error) ==
				MH_ERROR_INVALID,
		"draw rejects misaligned and out-of-range triangle spans");
	dynamic_attributes(fixture, &mesh);
	reflection_updates(fixture, &mesh);

cleanup:
	vertices = mesh.geometry.vertices;
	array = mesh.geometry.array.handle;
	texture = pool.items[0].texture.handle;
	mh_webgl_mesh_destroy(&mesh);
	check(!glIsBuffer(vertices) && !glIsVertexArray(array) &&
			glIsTexture(texture),
		"mesh destruction leaves shared images alive");
	mh_webgl_images_destroy(&pool);
	check(!glIsTexture(texture),
		"image pool destruction releases its textures");
}

static void
powervr_effects(struct fixture *fixture, struct mh_webgl_mesh *mesh)
{
	struct mh_renderer_glenz glenz;
	struct mh_error error;
	float parameters[8];
	unsigned char rgba[4];

	memset(parameters, 0, sizeof(parameters));
	parameters[1] = parameters[6] = parameters[7] = 1;
	glUseProgram(fixture->powervr.handle);
	glBindBuffer(GL_UNIFORM_BUFFER, fixture->blocks[1]);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(parameters), parameters,
		GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, fixture->blocks[1]);

	if (!operation(mh_webgl_mesh_update(
			mesh, NULL, MH_SCENE_EFFECT_HALOS, 1, &error),
		&error))
		return;

	draw(mesh, 0);
	pixel(1, 1, 255, 127, 0,
		"PowerVR halo upload normalizes the first corner hue");
	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	check(rgba[3] == 112,
		"PowerVR halo upload retains peak intensity as four-bit opacity");
	check(mesh->colors[0] == 1 && mesh->colors[1] == 1 &&
			mesh->material_rgb[0] == 96,
		"PowerVR encoding retains original CPU colors");

	if (!operation(mh_webgl_mesh_update(mesh, NULL,
			MH_SCENE_EFFECT_PARTICLES, 1, &error),
		&error))
		return;

	draw(mesh, 0);
	pixel(1, 1, 255, 255, 255, "PowerVR particle upload uses white RGB");
	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	check(rgba[3] == 255,
		"PowerVR particle opacity saturates amplified average intensity");
	memset(&glenz, 0, sizeof(glenz));
	check(mh_webgl_mesh_glenz(mesh, &glenz, &error) ==
				MH_ERROR_UNSUPPORTED &&
			!mesh->glenz,
		"PowerVR mesh rejects unsupported Glenz effects");
}

static void
powervr_mesh(struct fixture *fixture)
{
	const struct mh_mesh *sources[1];
	struct mh_mesh source;
	struct mh_atlas atlas;
	struct mh_image images[3], page;
	struct mh_draw command;
	struct mh_vertex vertices[6];
	struct mh_webgl_images pool;
	struct mh_webgl_mesh mesh;
	struct mh_error error;
	unsigned char pixels[32 * 16 * 4], format;
	float raw[18];
	size_t pages[3];
	unsigned int x, y, i;

	memset(&source, 0, sizeof(source));
	memset(&atlas, 0, sizeof(atlas));
	memset(images, 0, sizeof(images));
	memset(&pool, 0, sizeof(pool));
	memset(&mesh, 0, sizeof(mesh));
	memcpy(vertices, fixture->vertices, sizeof(vertices));
	memset(raw, 0, sizeof(raw));
	raw[0] = 96;
	raw[1] = 48;

	for (i = 0; i < 6; i++) {
		vertices[i].color[0] = vertices[i].color[1] =
			vertices[i].color[2] = 1;
		vertices[i].uv[0] = vertices[i].uv[1] = 0.75f;
	}

	for (y = 0; y < 16; y++)
		for (x = 0; x < 32; x++)
			memcpy(pixels + (y * 32 + x) * 4,
				fixture->pixels +
					((y >= 8 ? 2 : 0) * 4 +
						(x >= 16 ? 2 : 0)) *
						4,
				4);

	page.width = 32;
	page.height = 16;
	page.rgba = pixels;
	format = MH_TEXTURE_ARGB4444;
	atlas.pages = &page;
	atlas.formats = &format;
	atlas.count = 1;
	images[0] = fixture->images[0];
	pages[0] = pages[2] = (size_t)-1;
	pages[1] = 0;
	command = fixture->draws[1];
	source.vertices = vertices;
	source.vertex_count = 6;
	source.material_rgb = raw;
	source.material_rgb_count = 6;
	source.draws = &command;
	source.draw_count = 1;
	source.textures = images;
	source.texture_count = 3;
	source.atlas = &atlas;
	source.atlas_images = pages;
	source.atlas_uv = fixture->atlas_uv;
	sources[0] = &source;

	if (!operation(mh_webgl_images_create(&pool, sources, 1, 1, &error),
		&error))
		return;

	check(pool.count == 2 && pool.bindings[0][2] == MH_WEBGL_NO_IMAGE,
		"PowerVR pool skips unused atlas textures");
	i = (unsigned int)pool.bindings[0][1];
	check(pool.items[i].source == &page &&
			pool.items[i].texture.width == 32 &&
			pool.items[i].texture.height == 32 &&
			pool.items[i].texture.levels == 1,
		"PowerVR pool resolves atlas pages and pads non-square images without mips");

	if (!operation(mh_webgl_mesh_create(&mesh, &source, &pool, 0, &error),
		&error))
		goto cleanup;

	if (!operation(mh_webgl_mesh_update(
			&mesh, NULL, MH_SCENE_EFFECT_SKY, 0, &error),
		&error))
		goto cleanup;

	command.count = 7;
	check(mh_webgl_mesh_update(&mesh, NULL, MH_SCENE_EFFECT_SKY, 0,
		&error) == MH_ERROR_INVALID &&
			mesh.vertices_pending,
		"rejected conversion keeps the earlier pending update");
	command.count = 6;

	material(fixture, 0, 0);
	draw(&mesh, 1);
	pixel(1, 1, 255, 255, 255,
		"PowerVR sky uses source UVs scaled to padded texture dimensions");
	powervr_effects(fixture, &mesh);

cleanup:
	mh_webgl_mesh_destroy(&mesh);
	mh_webgl_images_destroy(&pool);
	format = MH_TEXTURE_ARGB1555;
	check(mh_webgl_images_create(&pool, sources, 1, 1, &error) ==
				MH_ERROR_INVALID &&
			pool.items == NULL && pool.bindings == NULL,
		"PowerVR rejects unsupported texture precision without publishing resources");
}

static void
rejected_images(struct fixture *fixture)
{
	const struct mh_mesh *sources[2];
	struct mh_webgl_images pool;
	struct mh_image inconsistent[2];
	struct mh_error error;

	memset(&pool, 0, sizeof(pool));
	sources[0] = &fixture->source[0];
	sources[1] = &fixture->source[1];
	memcpy(inconsistent, fixture->images, sizeof(inconsistent));
	inconsistent[1].width = 2;
	fixture->source[1].textures = inconsistent;
	check(mh_webgl_images_create(&pool, sources, 2, 0, &error) ==
				MH_ERROR_INVALID &&
			pool.items == NULL && pool.bindings == NULL,
		"inconsistent shared image dimensions reject the pool atomically");
	fixture->source[1].textures = fixture->images;
	fixture->draws[1].texture = 2;
	check(mh_webgl_images_create(&pool, sources, 2, 0, &error) ==
				MH_ERROR_INVALID &&
			pool.items == NULL && pool.bindings == NULL,
		"invalid draw texture rejects the pool atomically");
	fixture->draws[1].texture = 1;
}

int
mh_webgl_mesh_check(void)
{
	struct fixture fixture;

	if (!open_fixture(&fixture))
		return 1;

	geometry_bindings();
	batched_uploads(&fixture);
	ordinary_meshes(&fixture);
	powervr_mesh(&fixture);
	rejected_images(&fixture);
	check(glGetError() == GL_NO_ERROR,
		"mesh fixtures leave no WebGL errors");
	glUseProgram(0);
	mh_webgl_program_destroy(&fixture.ordinary);
	mh_webgl_program_destroy(&fixture.glenz);
	mh_webgl_program_destroy(&fixture.powervr);
	glDeleteBuffers(2, fixture.blocks);
	emscripten_webgl_destroy_context(fixture.context);

	return failures != 0;
}

int
main(void)
{
	return 0;
}
