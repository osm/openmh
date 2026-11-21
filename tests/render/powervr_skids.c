#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

enum skid_case {
	NEAR_MARK,
	MIDDLE_MARK,
	FAR_MARK,
	VARYING_DEPTH,
	CLIPPED_NEAR,
	OCCLUDED_MARK,
	OVERLAPPING_MARKS,
	FAR_CORNER,
	COPLANAR_MARK,
	BEHIND_MARK,
	ALL_NEAR,
	FOGGED_NEAR,
	FOGGED_FAR,
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
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = 1;

	/* Both samples lie inside the near split and use the first color. */
	for (x = 200; x <= 240; x += 40) {
		pixel = (const unsigned char *)rgba->pixels +
			120 * rgba->pitch + x * 4;

		for (channel = 0; channel < 3; channel++)
			if (fabs(pixel[channel] - expected) > 1.1) {
				fprintf(stderr,
					"pixel %u channel %u: %u expected %.6f\n",
					x, channel, pixel[channel], expected);
				success = 0;
			}
	}

	SDL_DestroySurface(rgba);

	return success;
}

int
main(int argc, char *argv[])
{
	struct mh_mesh meshes[3];
	const struct mh_mesh *list[3];
	struct mh_vertex vertices[3][6];
	struct mh_draw draws[3];
	struct mh_image textures[2];
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_gpu_renderer *gpu;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char black[32 * 32 * 4];
	unsigned char formats[2] = {0, MH_TEXTURE_RGB555};
	const double cue[3] = {128, 128, 128};
	float raw[3][18], left, right, ground;
	double expected;
	unsigned int i, test;
	int success;

	if (argc != 2)
		return 2;

	memset(black, 0, sizeof(black));
	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1] = textures[0];
	textures[1].rgba = black;
	textures[1].width = textures[1].height = 32;

	for (i = 0; i < 3; i++) {
		make_quad(vertices[i], raw[i], 10, 10, 128);
		meshes[i].vertices = vertices[i];
		meshes[i].vertex_count = 6;
		meshes[i].material_rgb = raw[i];
		meshes[i].material_rgb_count = 6;
		meshes[i].draws = &draws[i];
		meshes[i].draw_count = 1;
		meshes[i].textures = textures;
		meshes[i].texture_count = 2;
		meshes[i].texture_formats = formats;
		meshes[i].texture_format_count = 2;
		meshes[i].environment_texture = 1;
		draws[i].count = 6;
		draws[i].prelit = 1;
		list[i] = &meshes[i];
	}

	/* Marks ignore source colors, reflection, texture and alpha testing. */
	draws[1].texture = draws[1].environment = draws[1].alpha_test = 1;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	gpu = mh_gpu_powervr_open(NULL, list, 3, 320, 240, 0);
	success = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 300,
			&projection, NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 300, &fog, NULL) == MH_ERROR_NONE &&
		mh_gpu_renderer_set_fog(gpu, &fog);

	for (test = 0; test < SKID_CASES && success; test++) {
		left = right = 32;
		ground = 128;
		expected = 127 * 11.0 / 16;

		switch (test) {
		case MIDDLE_MARK:
			left = right = 60;
			expected = 127 * 11.0 / 16;
			break;

		case FAR_MARK:
			left = right = 96;
			break;

		case VARYING_DEPTH:
			right = 96;
			break;

		case CLIPPED_NEAR:
			left = .25f;
			expected = 127 * 11.0 / 16;
			break;

		case OCCLUDED_MARK:
			ground = 4;
			expected = 127;
			break;

		case OVERLAPPING_MARKS:
			expected = 127 * 11.0 / 16 * 11.0 / 16;
			break;

		case FAR_CORNER:
			right = 301;
			expected = 127;
			break;

		case COPLANAR_MARK:
			left = right = ground = 32;
			break;

		case BEHIND_MARK:
			left = right = 32.1f;
			ground = 32;
			expected = 127;
			break;

		case ALL_NEAR:
			left = right = .25f;
			expected = 127;
			break;

		case FOGGED_NEAR:
			expected += 127 * (7.0 / 256) * (5.0 / 16);
			break;

		case FOGGED_FAR:
			left = right = 96;
			expected += 127 * (78.0 / 256) * (5.0 / 16);
			break;

		default:
			break;
		}

		make_quad(vertices[0], raw[0], ground, ground, 127);
		make_quad(vertices[1], raw[1], left, right, 0);
		make_quad(vertices[2], raw[2], 96, 96, 0);
		success = mh_gpu_renderer_set_fog(
				gpu, test >= FOGGED_NEAR ? &fog : NULL) &&
			mh_gpu_renderer_mesh_lit(gpu, 0, vertices[0]) &&
			mh_gpu_renderer_mesh_skids(gpu, 1) &&
			mh_gpu_renderer_mesh_skids(gpu, 2) &&
			mh_gpu_renderer_mesh_visible(
				gpu, 2, test == OVERLAPPING_MARKS) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 1, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!success)
			fprintf(stderr, "PowerVR skid case %u failed: %s\n",
				test, SDL_GetError());
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (success)
		puts("Thirteen PowerVR skid frames: constant four-bit transparency, near/far clipping, unshifted reciprocal depth, overlap, fog, texture bypass and occlusion pass");

	return !success;
}
