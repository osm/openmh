#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/window/sdl_provider.h"
#include "engine/render/matrix.h"
#include "interfaces/render/backend.h"
#ifdef __EMSCRIPTEN__
#include "backends/render/webgl_provider.h"
#define openmh_backend mh_webgl_openmh_backend
#else
#include "backends/render/sdl_gpu_provider.h"
#define openmh_backend mh_sdl_gpu_openmh_backend
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#value);                                       \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
matches(const unsigned char *pixel, const unsigned int rgb[3])
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (abs((int)pixel[i] - (int)rgb[i]) > 1)
			return 0;

	return 1;
}

static int
picture(struct mh_renderer *renderer, const unsigned int lit[3])
{
	static const unsigned int emissive[3] = {40, 80, 120};
	struct mh_image image;
	struct mh_error error;
	const unsigned char *a, *b;
	int success;

	memset(&image, 0, sizeof(image));

	if (!mh_renderer_scene_frame(renderer, mh_render_identity,
		mh_render_identity, NULL, 0) ||
		mh_renderer_read_pixels(renderer, &image, &error) !=
			MH_ERROR_NONE)
		return 0;

	a = image.rgba + (60 * image.width + 80) * 4;
	b = image.rgba + (140 * image.width + 240) * 4;
	success = (matches(a, lit) && matches(b, emissive)) ||
		(matches(b, lit) && matches(a, emissive));

	if (!success)
		fprintf(stderr, "pixels %u,%u,%u / %u,%u,%u; lit %u,%u,%u\n",
			a[0], a[1], a[2], b[0], b[1], b[2], lit[0], lit[1],
			lit[2]);

	free(image.rgba);

	return success;
}

int
mh_openmh_render_check(void)
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	static const unsigned int ambient[3] = {50, 40, 30};
	static const unsigned int dark[3] = {0, 0, 0};
	struct mh_vertex vertices[6], original[6];
	struct mh_mesh_face faces[2];
	struct mh_mesh mesh;
	struct mh_draw draw;
	struct mh_image texture;
	unsigned char white[4];
	struct mh_world world;
	struct mh_world_cell cell;
	struct mh_world_light_link link;
	struct mh_point_light light;
	struct mh_model_lighting settings;
	struct mh_prepared_light prepared;
	struct mh_renderer *renderer;
	struct mh_window *window;
	struct mh_error error;
	const struct mh_mesh *source;
	float shaded[3];
	double origin[3], eye[3], normal[3], zero[3];
	unsigned int expected[3], i;

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(vertices, 0, sizeof(vertices));
	memset(faces, 0, sizeof(faces));
	memset(&world, 0, sizeof(world));
	memset(&cell, 0, sizeof(cell));
	memset(&link, 0, sizeof(link));
	memset(&light, 0, sizeof(light));
	memset(&settings, 0, sizeof(settings));
	memset(&texture, 0, sizeof(texture));
	memset(white, 255, sizeof(white));
	texture.width = texture.height = 1;
	texture.rgba = white;

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = .5f;
		vertices[i].normal[2] = 1;
		vertices[i].opacity = 1;
	}

	memcpy(original, vertices, sizeof(original));

	for (i = 0; i < 2; i++) {
		faces[i].first = i * 3;
		faces[i].count = 3;
		faces[i].type = 8;
	}

	faces[0].color[0] = 100;
	faces[0].color[1] = 80;
	faces[0].color[2] = 60;
	faces[1].flags = 0x10;
	faces[1].color[0] = 40;
	faces[1].color[1] = 80;
	faces[1].color[2] = 120;
	mesh.vertices = vertices;
	mesh.vertex_count = draw.count = 6;
	mesh.faces = faces;
	mesh.face_count = 2;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &texture;
	mesh.texture_count = 1;
	world.width = world.height = world.cell_count = 1;
	world.cell_size[0] = world.cell_size[1] = 10;
	world.cells = &cell;
	world.links = &link;
	world.lights = &light;
	cell.light_head = MH_WORLD_NO_LIGHT;
	link.next = MH_WORLD_NO_LIGHT;
	settings.mode = MH_LIGHT_OBJECT;
	settings.intensity = 1;

	for (i = 0; i < 3; i++) {
		settings.ambient[i] = .5f / 1.4f;
		settings.brightness[i] = 1;
		origin[i] = eye[i] = normal[i] = zero[i] = 0;
	}

	eye[2] = settings.eye[2] = 2;
	normal[2] = 1;
	window = NULL;
#ifndef __EMSCRIPTEN__
	mh_sdl_window_offscreen(0);
#endif
	CHECK(mh_window_system_acquire(mh_sdl_window_system(), NULL, &error) ==
		MH_ERROR_NONE);
#ifdef __EMSCRIPTEN__
	CHECK(mh_window_open(mh_sdl_window_system(), "OpenMH lighting", 320,
		200, 0, &window, &error) == MH_ERROR_NONE);
#endif
	source = &mesh;
	CHECK(mh_renderer_open(openmh_backend(), window, &source, 1, 320, 200,
		0, &renderer, &error) == MH_ERROR_NONE);
	CHECK(mh_renderer_world_lighting(renderer, 0, 0, &world, &settings,
		&error) == MH_ERROR_NONE);
	CHECK(picture(renderer, ambient));
	puts("OpenMH GPU ambient and distinct emissive materials");
	memset(settings.ambient, 0, sizeof(settings.ambient));
	CHECK(mh_renderer_world_lighting(renderer, 0, 0, &world, &settings,
		&error) == MH_ERROR_NONE);
	CHECK(picture(renderer, dark));
	puts("OpenMH lighting changes without rewriting source vertices");
	cell.light_head = 0;
	world.light_count = world.link_count = 1;
	light.position[2] = 2;
	light.radius = 10;
	light.intensity = .5;
	light.color[0] = .5;
	light.color[1] = .3;
	light.color[2] = .2;
	CHECK(mh_light_prepare(&light, origin, eye, 1, &prepared, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_light_shade_mode(&prepared, 1, normal, zero, MH_LIGHT_OBJECT,
		shaded, &error) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		expected[i] =
			(unsigned int)floor(faces[0].color[i] * shaded[i] + .5);

	CHECK(mh_renderer_world_lighting(renderer, 0, 0, &world, &settings,
		&error) == MH_ERROR_NONE);
	CHECK(picture(renderer, expected));
	puts("OpenMH GPU diffuse and specular agree with the CPU reference within one color step");
	CHECK(mh_renderer_world_lighting(renderer, 0, 0, NULL, NULL, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_world_lighting(renderer, 0, 0, &world, &settings,
		&error) == MH_ERROR_NONE);
	CHECK(picture(renderer, expected));
	puts("OpenMH lighting restores after a dedicated effect clears it");
	CHECK(memcmp(vertices, original, sizeof(vertices)) == 0);
	CHECK(mh_renderer_profile(renderer) == MH_RENDER_OPENMH);
	mh_renderer_destroy(renderer);

	if (window != NULL)
		mh_window_destroy(window);

	mh_window_system_release(mh_sdl_window_system());

	return 0;
}

int
main(void)
{
#ifdef __EMSCRIPTEN__
	return 0;
#else

	return mh_openmh_render_check();
#endif
}
