#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum face_case {
	CLIPPED_SMOOTH,
	CLIPPED_FLAT,
	SIGNED_PACK,
	FOG_NEAR,
	NO_FOG,
	FOG_FAR,
	FACE_CASES
};

enum unavailable_case {
	UNAVAILABLE_SMOOTH,
	UNAVAILABLE_FLAT,
	MATERIAL_OVERRIDE,
	REVERSED_DEPTH,
	OCCLUDED,
	MATERIAL_RESTORED,
	UNAVAILABLE_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
capture_matches(const char *path, unsigned int y, const double expected[3])
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int channel;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	pixel = (const unsigned char *)rgba->pixels + y * rgba->pitch + 160 * 4;
	ok = 1;

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected[channel]) > .6) {
			fprintf(stderr, "channel %u: %u expected %.6f\n",
				channel, pixel[channel], expected[channel]);
			ok = 0;
		}

	SDL_DestroySurface(rgba);

	return ok;
}

static int
check_unavailable(const char *path)
{
	static const float xy[3][2] = {
		{0, .25f}, {.75f, -.75f}, {-.75f, -.75f}};

	static const double cue[3] = {20, 30, 50};
	static const double expected[UNAVAILABLE_CASES][3] = {{26, 50, 90},
		{25, 50, 90}, {70, 100, 140}, {70, 100, 140}, {20, 30, 50},
		{0, 0, 0}};

	struct mh_mesh meshes[3];
	const struct mh_mesh *list[3];
	struct mh_vertex vertices[3][3];
	struct mh_mesh_primitive primitives[3];
	struct mh_draw draws[3];
	struct mh_image textures[2];
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_renderer *gpu;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char black[4] = {0, 0, 0, 0};
	float raw[3][9], depth;
	unsigned int m, i, c, test;
	int ok;

	memset(meshes, 0, sizeof(meshes));
	memset(vertices, 0, sizeof(vertices));
	memset(primitives, 0, sizeof(primitives));
	memset(draws, 0, sizeof(draws));
	memset(raw, 0, sizeof(raw));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1] = textures[0];
	textures[1].rgba = black;

	for (m = 0; m < 3; m++) {
		meshes[m].vertices = vertices[m];
		meshes[m].vertex_count = 3;
		meshes[m].material_rgb = raw[m];
		meshes[m].material_rgb_count = 3;
		meshes[m].draws = &draws[m];
		meshes[m].draw_count = 1;
		meshes[m].primitives = &primitives[m];
		meshes[m].primitive_count = 1;
		meshes[m].textures = textures;
		meshes[m].texture_count = 2;
		meshes[m].environment_texture = 1;
		draws[m].count = primitives[m].count = 3;
		draws[m].prelit = 1;
		list[m] = &meshes[m];
	}

	gpu = mh_sdl_renderer_create_scene_sized(NULL, list, 3, 320, 240);
	ok = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, 16, 128, &projection,
			NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 128, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(gpu, &fog);

	for (test = 0; test < UNAVAILABLE_CASES && ok; test++) {
		for (m = 0; m < 3 && ok; m++) {
			primitives[m].type = test == UNAVAILABLE_FLAT ? 0 : 4;

			/*
			 * Unavailability overrides alpha, texture and
			 * reflection.
			 */
			draws[m].texture = draws[m].environment =
				draws[m].alpha_test =
					m != 0 && test >= MATERIAL_OVERRIDE &&
				test != MATERIAL_RESTORED;

			if (test == MATERIAL_RESTORED && m != 0)
				draws[m].texture = 1;

			depth = m == 0 ? (test == OCCLUDED ? 20 : 112)
				: (m == 1) != (test == REVERSED_DEPTH) ? 24
								: 64;

			for (i = 0; i < 3; i++) {
				vertices[m][i].position[0] = xy[i][0] * depth;
				vertices[m][i].position[1] = xy[i][1] * depth;
				vertices[m][i].position[2] = -depth;

				for (c = 0; c < 3; c++)
					raw[m][i * 3 + c] = m == 0
						? (float)cue[c]
						: (m - 1) * 30 + (c + 1) * 10 +
							.875f;

				if (test < MATERIAL_OVERRIDE && m == 1) {
					vertices[m][i].position[0] =
						xy[i][0] * 16;
					vertices[m][i].position[1] =
						xy[i][1] * 16;
					vertices[m][i].position[2] =
						i == 0 ? -8 : -24;
					raw[m][i * 3] =
						i == 0 ? 5.875f : 6.875f;
					raw[m][i * 3 + 1] = 20.875f;
					raw[m][i * 3 + 2] = 40.875f;
				}
			}

			ok = mh_renderer_mesh_visible(gpu, m,
				test >= MATERIAL_OVERRIDE || m != 2) &&
				(m == 0 || test == MATERIAL_RESTORED
						? mh_renderer_mesh_lit(
							gpu, m, vertices[m])
						: mh_renderer_mesh_unavailable(
							gpu, m, vertices[m]));
		}

		if (ok)
			ok = mh_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 1, NULL, NULL, NULL,
				NULL) == MH_ERROR_NONE &&
				mh_renderer_capture(gpu, path) &&
				capture_matches(path, 180, expected[test]);

		if (!ok)
			fprintf(stderr,
				"Direct3D unavailable frame %u failed: %s\n",
				test, SDL_GetError());
	}

	mh_renderer_destroy(gpu);

	return ok;
}

int
main(int argc, char *argv[])
{
	static const float triangle[3][3] = {
		{0, .25f, -.5f}, {.75f, -.75f, -1.5f}, {-.75f, -.75f, -1.5f}};

	static const double expected[FACE_CASES][3] = {{1, 20, 40}, {0, 20, 40},
		{255, 255, 255}, {60, 55, 55}, {100, 80, 60}, {20, 30, 50}};

	static const double cue[3] = {20, 30, 50};
	struct mh_mesh mesh;
	const struct mh_mesh *meshes[1];
	struct mh_vertex vertices[3];
	struct mh_mesh_primitive primitive;
	struct mh_draw draw;
	struct mh_image image;
	struct mh_projection projection;
	struct mh_renderer *gpu;
	struct mh_fog fog;
	unsigned char white[4] = {255, 255, 255, 255};
	float raw[9];
	unsigned int i, axis, test, y;
	int ok;

	if (argc != 2 || !SDL_Init(SDL_INIT_VIDEO))
		return 2;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&primitive, 0, sizeof(primitive));
	memset(&draw, 0, sizeof(draw));
	image.width = image.height = 1;
	image.rgba = white;
	mesh.vertices = vertices;
	mesh.vertex_count = 3;
	mesh.material_rgb = raw;
	mesh.material_rgb_count = 3;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	mesh.textures = &image;
	mesh.texture_count = 1;
	draw.count = primitive.count = 3;
	draw.prelit = 1;

	for (i = 0; i < 9; i++)
		raw[i] = 0;

	meshes[0] = &mesh;
	gpu = mh_sdl_renderer_create_scene_sized(NULL, meshes, 1, 320, 240);
	ok = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, 16, 128, &projection,
			NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 128, &fog, NULL) == MH_ERROR_NONE;

	for (test = 0; test < FACE_CASES && ok; test++) {
		primitive.type =
			test == CLIPPED_FLAT || test == SIGNED_PACK ? 0 : 4;

		for (i = 0; i < 3; i++) {
			memcpy(vertices[i].position, triangle[i],
				sizeof(triangle[i]));
			raw[i * 3] = i == 0 ? .875f : 1.875f;
			raw[i * 3 + 1] = 20.875f;
			raw[i * 3 + 2] = 40.875f;

			if (test == SIGNED_PACK) {
				raw[i * 3] = raw[i * 3 + 1] = 0;
				raw[i * 3 + 2] = -1;
			}

			if (test >= FOG_NEAR) {
				vertices[i].position[0] *= 4;
				vertices[i].position[1] *= 4;
				vertices[i].position[2] =
					test == FOG_FAR ? -7.2f : -3.6f;
				raw[i * 3] = 100;
				raw[i * 3 + 1] = 80;
				raw[i * 3 + 2] = 60;
			}

			for (axis = 0; axis < 3; axis++)
				vertices[i].position[axis] *= 16;
		}

		y = test < FOG_NEAR ? 180 : 130;
		ok = mh_renderer_set_fog(gpu,
			test == FOG_NEAR || test == FOG_FAR ? &fog
								: NULL) &&
			mh_renderer_mesh_lit(gpu, 0, vertices) &&
			mh_renderer_scene_frame_lens(gpu, &projection, identity,
				NULL, 0, NULL, NULL, NULL,
				NULL) == MH_ERROR_NONE &&
			mh_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], y, expected[test]);

		if (!ok)
			fprintf(stderr,
				"Direct3D prepared frame %u failed: %s\n", test,
				SDL_GetError());
	}

	mh_renderer_destroy(gpu);

	if (ok)
		ok = check_unavailable(argv[1]);

	SDL_Quit();

	if (ok)
		puts("Direct3D: twelve GPU frames pass clipping/packing, fog, unavailable blending/depth/material overrides and restoration");

	return ok ? 0 : 1;
}
