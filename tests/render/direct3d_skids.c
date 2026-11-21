#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum skid_mesh {
	GROUND,
	MARK,
	OVERLAP,
	MESH_COUNT
};

enum skid_case {
	NEAR_MARK,
	MIDDLE_MARK,
	FADED_MARK,
	VARYING_DEPTH,
	CLIPPED_NEAR,
	OCCLUDED_MARK,
	OVERLAPPING_MARKS,
	FAR_CORNER,
	LIFTED_MARK,
	DEEP_MARK,
	ALL_NEAR,
	SKID_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void
make_quad(struct mh_vertex vertices[6], float raw[18], float left, float right,
	float color)
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	float depth;
	unsigned int i, channel;

	memset(vertices, 0, 6 * sizeof(*vertices));

	for (i = 0; i < 6; i++) {
		depth = xy[i][0] < 0 ? left : right;
		vertices[i].position[0] = xy[i][0] * depth;
		vertices[i].position[1] = xy[i][1] * depth * .75f;
		vertices[i].position[2] = -depth;

		for (channel = 0; channel < 3; channel++) {
			vertices[i].color[channel] = color / 255;
			raw[i * 3 + channel] = color;
		}
	}
}

static int
capture_matches(const char *path, double expected)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int x, channel;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	ok = 1;

	/* Both samples lie inside the near split and use the first color. */
	for (x = 200; x <= 240; x += 40) {
		pixel = (const unsigned char *)rgba->pixels +
			120 * rgba->pitch + x * 4;

		for (channel = 0; channel < 3; channel++)
			if (fabs(pixel[channel] - expected) > 1.1) {
				fprintf(stderr,
					"pixel %u channel %u: %u expected %.6f\n",
					x, channel, pixel[channel], expected);
				ok = 0;
			}
	}

	SDL_DestroySurface(rgba);

	return ok;
}

int
main(int argc, char *argv[])
{
	struct mh_mesh meshes[MESH_COUNT];
	const struct mh_mesh *list[MESH_COUNT];
	struct mh_vertex vertices[MESH_COUNT][6];
	struct mh_draw draws[MESH_COUNT];
	struct mh_image textures[2];
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_renderer *gpu;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char black[4] = {0, 0, 0, 0};
	const double cue[3] = {128, 128, 128};
	float raw[MESH_COUNT][18], left, right, ground;
	double expected;
	unsigned int i, test;
	int ok;

	if (argc != 2)
		return 2;

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1] = textures[0];
	textures[1].rgba = black;

	for (i = 0; i < MESH_COUNT; i++) {
		make_quad(vertices[i], raw[i], 10, 10, 128);
		meshes[i].vertices = vertices[i];
		meshes[i].vertex_count = 6;
		meshes[i].material_rgb = raw[i];
		meshes[i].material_rgb_count = 6;
		meshes[i].draws = &draws[i];
		meshes[i].draw_count = 1;
		meshes[i].textures = textures;
		meshes[i].texture_count = 2;
		meshes[i].environment_texture = 1;
		draws[i].count = 6;
		draws[i].prelit = 1;
		list[i] = &meshes[i];
	}

	/* Marks ignore source colors, reflection, texture and alpha testing. */
	draws[MARK].texture = draws[MARK].environment = draws[MARK].alpha_test =
		1;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	gpu = mh_sdl_renderer_create_scene_sized(
		NULL, list, MESH_COUNT, 320, 240);
	ok = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
			&projection, NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(gpu, &fog);

	for (test = 0; test < SKID_CASES && ok; test++) {
		left = right = 10;
		ground = 160;
		expected = 128 * 191.0 / 255;

		switch (test) {
		case MIDDLE_MARK:
			left = right = 30;
			expected = 128 * 213.0 / 255;
			break;

		case FADED_MARK:
			left = right = 90;
			expected = 128;
			break;

		case VARYING_DEPTH:
			right = 30;
			break;

		case CLIPPED_NEAR:
			left = .25f;
			expected = 128 * 155.0 / 255;
			break;

		case OCCLUDED_MARK:
			ground = 4;
			expected = 128;
			break;

		case OVERLAPPING_MARKS:
			expected = 128 * 191.0 / 255 * 213.0 / 255;
			break;

		case FAR_CORNER:
			right = 181;
			expected = 128;
			break;

		case LIFTED_MARK:
			left = right = 10.1f;
			ground = 10;
			break;

		case DEEP_MARK:
			left = right = 10.3f;
			ground = 10;
			expected = 128;
			break;

		case ALL_NEAR:
			left = right = .25f;
			expected = 128;
			break;

		default:
			break;
		}

		make_quad(vertices[GROUND], raw[GROUND], ground, ground, 128);
		make_quad(vertices[MARK], raw[MARK], left, right, 0);
		make_quad(vertices[OVERLAP], raw[OVERLAP], 30, 30, 0);
		ok = mh_renderer_mesh_lit(gpu, GROUND, vertices[GROUND]) &&
			mh_renderer_mesh_skids(gpu, MARK) &&
			mh_renderer_mesh_skids(gpu, OVERLAP) &&
			mh_renderer_mesh_visible(
				gpu, OVERLAP, test == OVERLAPPING_MARKS) &&
			mh_renderer_scene_frame_lens(gpu, &projection, identity,
				NULL, 1, NULL, NULL, NULL,
				NULL) == MH_ERROR_NONE &&
			mh_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!ok)
			fprintf(stderr, "Direct3D skid case %u failed: %s\n",
				test, SDL_GetError());
	}

	mh_renderer_destroy(gpu);
	SDL_Quit();

	if (ok)
		puts("Eleven Direct3D skid frames: flat fade, near/far clipping, depth offset, overlap, fog/texture bypass and occlusion pass");

	return !ok;
}
