#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

enum shadow_case {
	NEAR_MARK,
	MIDDLE_MARK,
	FAR_MARK,
	VARYING_DEPTH,
	SOFT_EDGE,
	CLIPPED_NEAR,
	CLOSE_MARK,
	PALE_MARK,
	TRANSPARENT_MARK,
	RAMP_EDGE,
	BIASED_MARK,
	OCCLUDED_MARK,
	OVERLAPPING_MARKS,
	FAR_CORNER,
	COPLANAR_MARK,
	BEHIND_MARK,
	ALL_NEAR,
	FOGGED_NEAR,
	FOGGED_FAR,
	SHADOW_CASES
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
capture_matches(const char *path, double expected, int soft_edge)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int x, channel;
	double t, left, right, u;
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = 1;

	/* Samples lie inside the shadow quad, away from rasterization edges. */
	for (x = 200; x <= 240; x += 40) {
		if (soft_edge) {
			/*
			 * Independent perspective interpolation of the captured
			 * ramp.
			 */
			t = (x + .5 - 32) / 256;
			left = (1 - t) / 31.9;
			right = t / 95.9;
			u = (left * (129 + 155.0 / 16) +
				right * (129 + 35.0 / 16)) /
				(left + right);
			expected = 127 * (15 - (u - 129.5)) / 16;
		}

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
	unsigned char atlas[256 * 256 * 4];
	unsigned char formats[2] = {0, MH_TEXTURE_ARGB4444};
	const double cue[3] = {128, 128, 128};
	float raw[3][18], left, right, ground, transmission;
	double expected;
	unsigned int i, x, y, test;
	int success;

	if (argc != 2)
		return 2;

	memset(atlas, 0, sizeof(atlas));

	/*
	 * Expected ramp: rows 64..66, alpha 0,17,...255 at 129..144.
	 */
	for (y = 64; y <= 66; y++)
		for (x = 129; x <= 144; x++)
			atlas[(y * 256 + x) * 4 + 3] =
				(unsigned char)((x - 129) * 17);

	for (y = 64; y <= 66; y++) {
		atlas[(y * 256 + 145) * 4] = 255;
		atlas[(y * 256 + 146) * 4] = 255;
	}

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1] = textures[0];
	textures[1].rgba = atlas;
	textures[1].width = textures[1].height = 256;

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

	/* Only texture alpha contributes; shadow RGB is forced to black. */
	draws[1].texture = draws[2].texture = 1;
	meshes[1].material_rgb = meshes[2].material_rgb = NULL;
	meshes[1].material_rgb_count = meshes[2].material_rgb_count = 0;
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

	for (test = 0; test < SHADOW_CASES && success; test++) {
		left = right = 32;
		ground = 128;
		transmission = 127;
		expected = 127 * 7.5 / 16;

		switch (test) {
		case MIDDLE_MARK:
			left = right = 60;
			expected = 127 * 7.5 / 16;
			break;

		case FAR_MARK:
			left = right = 96;
			break;

		case SOFT_EDGE:
		case VARYING_DEPTH:
			right = 96;
			break;

		case CLOSE_MARK:
			left = right = .75f;
			break;

		case PALE_MARK:
			transmission = 223;
			expected = 127 * 13.5 / 16;
			break;

		case TRANSPARENT_MARK:
			transmission = 255;
			expected = 127;
			break;

		case RAMP_EDGE:
			transmission = 0;
			expected = 127 * 6.5625 / 16;
			break;

		case BIASED_MARK:
			left = right = 32.05f;
			ground = 32;
			break;

		case CLIPPED_NEAR:
			left = .25f;
			expected = 127;
			break;

		case OCCLUDED_MARK:
			ground = 4;
			expected = 127;
			break;

		case OVERLAPPING_MARKS:
			expected = 127 * 7.5 / 16 * 7.5 / 16;
			break;

		case FAR_CORNER:
			right = 301;
			expected = 127;
			break;

		case COPLANAR_MARK:
			left = right = ground = 32;
			break;

		case BEHIND_MARK:
			left = right = 32.2f;
			ground = 32;
			expected = 127;
			break;

		case ALL_NEAR:
			left = right = .25f;
			expected = 127;
			break;

		case FOGGED_NEAR:
			expected += 127 * (7.0 / 256) * (8.5 / 16);
			break;

		case FOGGED_FAR:
			left = right = 96;
			expected += 127 * (78.0 / 256) * (8.5 / 16);
			break;

		default:
			break;
		}

		make_quad(vertices[0], raw[0], ground, ground, 127);
		make_quad(vertices[1], raw[1], left, right, transmission);
		make_quad(vertices[2], raw[2], 96, 96, 127);

		if (test == SOFT_EDGE)
			for (i = 0; i < 6; i++)
				vertices[1][i].color[0] =
					(vertices[1][i].position[0] < 0 ? 100
									: 220) /
					255.0f;

		success = mh_gpu_renderer_set_fog(
				gpu, test >= FOGGED_NEAR ? &fog : NULL) &&
			mh_gpu_renderer_mesh_lit(gpu, 0, vertices[0]) &&
			mh_gpu_renderer_mesh_shadows(gpu, 1) &&
			mh_gpu_renderer_mesh_shadows(gpu, 2) &&
			mh_gpu_renderer_mesh_visible(
				gpu, 2, test == OVERLAPPING_MARKS) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 1, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected, test == SOFT_EDGE);

		if (!success)
			fprintf(stderr, "PowerVR shadow case %u failed: %s\n",
				test, SDL_GetError());
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (success)
		puts("Nineteen PowerVR shadow frames: atlas alpha, whole-quad clipping, reciprocal-depth bias, overlap, fog and occlusion pass");

	return !success;
}
