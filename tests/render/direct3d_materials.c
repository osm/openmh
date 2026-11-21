#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum {
	ROAD,
	REFLECTION,
	ALPHA,
	PARTICLE,
	MESH_COUNT,
	VERTEX_COUNT = 6
};

enum material_case {
	PHONG_NEAR,
	PHONG_FAR,
	PHONG_FRONT,
	ALPHA_NEAR,
	ALPHA_FAR,
	ALPHA_CUTOUT,
	ORDINARY_NEAR,
	REFLECTION_NEAR,
	PHONG_ALPHA,
	ALPHA_PARTICLE,
	MATERIAL_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void
make_quad(struct mh_vertex vertices[VERTEX_COUNT], float raw[VERTEX_COUNT * 3],
	float depth, float color, int camera_space)
{
	static const float xy[VERTEX_COUNT][2] = {{-.8f, -.8f}, {.8f, -.8f},
		{.8f, .8f}, {-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	float distance;
	unsigned int i, channel;

	memset(vertices, 0, VERTEX_COUNT * sizeof(*vertices));
	distance = .5f / (1 - depth);

	for (i = 0; i < VERTEX_COUNT; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = depth;

		if (camera_space) {
			vertices[i].position[0] *= distance;
			vertices[i].position[1] *= distance * .75f;
			vertices[i].position[2] = -distance;
		}

		for (channel = 0; channel < 3; channel++) {
			raw[i * 3 + channel] = color;
			vertices[i].color[channel] = color / 255;
		}
	}
}

static int
capture_matches(const char *path, unsigned int expected)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int x, y, c;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	ok = 1;

	for (y = 100; y < 140 && ok; y++) {
		for (x = 120; x < 200 && ok; x++) {
			pixel = (const unsigned char *)rgba->pixels +
				y * rgba->pitch + x * 4;

			for (c = 0; c < 3; c++) {
				if (fabs((double)pixel[c] - expected) > 1.1) {
					fprintf(stderr,
						"pixel %u,%u channel %u: %u expected %u\n",
						x, y, c, pixel[c], expected);
					ok = 0;
					break;
				}
			}
		}
	}

	SDL_DestroySurface(rgba);

	return ok;
}

int
main(int argc, char *argv[])
{
	static const float colors[MESH_COUNT] = {64, 64, 128, 16};
	struct mh_mesh meshes[MESH_COUNT];
	const struct mh_mesh *inputs[MESH_COUNT];
	struct mh_vertex vertices[MESH_COUNT][VERTEX_COUNT];
	struct mh_draw draws[MESH_COUNT];
	struct mh_image textures[3];
	struct mh_glenz_coordinates coordinates[VERTEX_COUNT];
	struct mh_renderer_glenz material;
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_renderer *gpu;
	unsigned char texels[3][4] = {
		{255, 255, 255, 255}, {128, 128, 128, 255}, {255, 255, 255, 0}};

	const double cue[3] = {64, 64, 64};
	float raw[MESH_COUNT][VERTEX_COUNT * 3], depth;
	unsigned int m, test, direct3d, expected;
	int ok, visible, phong;

	if (argc != 2)
		return 2;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	memset(coordinates, 0, sizeof(coordinates));

	for (m = 0; m < 3; m++) {
		textures[m].width = textures[m].height = 1;
		textures[m].rgba = texels[m];
	}

	for (m = 0; m < MESH_COUNT; m++) {
		make_quad(vertices[m], raw[m], .95f, colors[m], 0);
		meshes[m].vertices = vertices[m];
		meshes[m].vertex_count = VERTEX_COUNT;
		meshes[m].material_rgb = raw[m];
		meshes[m].material_rgb_count = VERTEX_COUNT;
		meshes[m].draws = &draws[m];
		meshes[m].draw_count = 1;
		meshes[m].textures = textures;
		meshes[m].texture_count = 3;
		meshes[m].environment_texture = 1;
		draws[m].count = VERTEX_COUNT;
		draws[m].prelit = 1;
		draws[m].environment = m == REFLECTION;
		inputs[m] = &meshes[m];
	}

	material.coordinates = coordinates;
	material.count = VERTEX_COUNT;
	material.reflection_texture = 0;
	material.phong_texture = 1;
	gpu = mh_sdl_renderer_create_scene_sized(
		NULL, inputs, MESH_COUNT, 320, 240);
	ok = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
			&projection, NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(gpu, &fog) &&
		mh_renderer_mesh_particles(gpu, PARTICLE);

	for (test = 0; test < MATERIAL_CASES && ok; test++) {
		/*
		 * Alternate profiles on the same renderer to catch bias
		 * leakage.
		 */
		for (direct3d = 0; direct3d < 2 && ok; direct3d++) {
			phong = test <= PHONG_FRONT || test == PHONG_ALPHA;
			expected = 64;

			if (test == PHONG_FRONT || test == PHONG_ALPHA ||
				(test == PHONG_NEAR && direct3d))
				expected = 96;
			else if ((test == ALPHA_NEAR && direct3d) ||
				test == ALPHA_PARTICLE)
				expected = 128;

			for (m = 0; m < MESH_COUNT; m++) {
				visible = m == ROAD ||
					(m == REFLECTION &&
						(phong ||
							test == REFLECTION_NEAR)) ||
					(m == ALPHA && test >= ALPHA_NEAR &&
						test != REFLECTION_NEAR) ||
					(m == PARTICLE &&
						test == ALPHA_PARTICLE);
				depth = .9500005f;

				if (m == ROAD ||
					(m == REFLECTION &&
						test == PHONG_ALPHA) ||
					(m == ALPHA && test == ALPHA_PARTICLE))
					depth = .95f;
				else if (test == PHONG_FAR || test == ALPHA_FAR)
					depth = .95001f;
				else if (test == PHONG_FRONT)
					depth = .94f;

				make_quad(vertices[m], raw[m], depth, colors[m],
					direct3d && m != PARTICLE);
				ok = ok &&
					mh_renderer_mesh_visible(
						gpu, m, visible);

				if (m != PARTICLE)
					ok = ok &&
						mh_renderer_mesh_lit(
							gpu, m, vertices[m]);
			}

			ok = ok && mh_renderer_mesh_particles(gpu, PARTICLE);
			draws[ALPHA].alpha_test = test != ORDINARY_NEAR;
			draws[ALPHA].texture = test == ALPHA_CUTOUT ? 2 : 0;
			ok = ok &&
				mh_renderer_mesh_glenz(gpu, REFLECTION,
					phong ? &material : NULL,
					NULL) == MH_ERROR_NONE;

			if (ok)
				ok = direct3d
					? mh_renderer_scene_frame_lens(gpu,
						&projection, identity, NULL,
						1, NULL, NULL, NULL,
						NULL) == MH_ERROR_NONE
					: mh_renderer_scene_frame(gpu, identity,
						identity, NULL, 1);

			ok = ok && mh_renderer_capture(gpu, argv[1]) &&
				capture_matches(argv[1], expected);

			if (!ok)
				fprintf(stderr,
					"Material case %u Direct3D=%u: %s\n",
					test, direct3d, SDL_GetError());
		}
	}

	mh_renderer_destroy(gpu);
	SDL_Quit();

	if (ok)
		puts("20 Glide/Direct3D material frames pass: Phong/alpha bias, empty Phong, cutout, occlusion, depth writes and profile isolation");

	return !ok;
}
