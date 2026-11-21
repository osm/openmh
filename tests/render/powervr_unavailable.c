#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

enum unavailable_case {
	NEAR_CAR,
	MIDDLE_CAR,
	FAR_CAR,
	VARYING_DEPTH,
	CLIPPED_NEAR,
	OCCLUDED_CAR,
	OVERLAPPING_CARS,
	FAR_CORNER,
	COPLANAR_CAR,
	BEHIND_CAR,
	ALL_NEAR,
	FOGGED_NEAR,
	FOGGED_FAR,
	UNAVAILABLE_CASES
};

static const double flat_color[3] = {0, 40, 70};

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
capture_matches(const char *path, const double expected[3])
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
			if (fabs(pixel[channel] - expected[channel]) > 1.1) {
				fprintf(stderr,
					"pixel %u channel %u: %u expected %.6f\n",
					x, channel, pixel[channel],
					expected[channel]);
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
	const double cue[3] = {0, 0, 0};
	float raw[3][18], left, right, ground;
	double expected[3], background, opacity;
	unsigned int i, j, test;
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

	/*
	 * Unavailable faces ignore vertex tint, reflection, texture and alpha
	 * testing.
	 */
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

	for (test = 0; test < UNAVAILABLE_CASES && success; test++) {
		left = right = 32;
		ground = 128;
		background = 127;
		opacity = 9.0 / 16;

		switch (test) {
		case MIDDLE_CAR:
			left = right = 60;
			break;

		case FAR_CAR:
			left = right = 96;
			break;

		case VARYING_DEPTH:
			right = 96;
			break;

		case CLIPPED_NEAR:
			left = .25f;
			break;

		case OCCLUDED_CAR:
			ground = 4;
			opacity = 0;
			break;

		case OVERLAPPING_CARS:
			opacity = 1 - 7.0 / 16 * 7.0 / 16;
			break;

		case FAR_CORNER:
			right = 301;
			opacity = 0;
			break;

		case COPLANAR_CAR:
			left = right = ground = 32;
			break;

		case BEHIND_CAR:
			left = right = 32.1f;
			ground = 32;
			opacity = 0;
			break;

		case ALL_NEAR:
			left = right = .25f;
			opacity = 0;
			break;

		case FOGGED_NEAR:
			background = 0;
			break;

		case FOGGED_FAR:
			left = right = 96;
			background = 0;
			break;

		default:
			break;
		}

		make_quad(vertices[0], raw[0], ground, ground, 127);

		/* Hide the backdrop in fog checks to isolate the car pass. */
		for (j = 0; j < 3; j++)
			expected[j] = background * (1 - opacity) +
				flat_color[j] * opacity;

		make_quad(vertices[1], raw[1], left, right, 0);
		make_quad(vertices[2], raw[2], 96, 96, 0);

		for (i = 1; i < 3; i++)
			for (j = 0; j < 6; j++) {
				raw[i][j * 3] = 60.5f;
				raw[i][j * 3 + 1] =
					(j == 0 || test == CLIPPED_NEAR) ? 40.9f
									: 5;
				raw[i][j * 3 + 2] =
					(j == 0 || test == CLIPPED_NEAR) ? 70
									: 10;
			}

		success = mh_gpu_renderer_set_fog(
				gpu, test >= FOGGED_NEAR ? &fog : NULL) &&
			mh_gpu_renderer_mesh_lit(gpu, 0, vertices[0]) &&
			mh_gpu_renderer_mesh_visible(
				gpu, 0, test < FOGGED_NEAR) &&
			mh_gpu_renderer_mesh_unavailable(gpu, 1, vertices[1]) &&
			mh_gpu_renderer_mesh_unavailable(gpu, 2, vertices[2]) &&
			mh_gpu_renderer_mesh_visible(
				gpu, 2, test == OVERLAPPING_CARS) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 1, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!success)
			fprintf(stderr,
				"PowerVR unavailable case %u failed: %s\n",
				test, SDL_GetError());
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (success)
		puts("Thirteen PowerVR unavailable frames: flat green/blue and deterministic zero red, four-bit transparency, clipping, reciprocal depth, overlap without depth writes, disabled fog, texture bypass and occlusion pass");

	return !success;
}
