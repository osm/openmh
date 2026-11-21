#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

enum halo_case {
	DIM_HALO,
	FULL_HALO,
	BLACK_HALO,
	BELOW_NIBBLE,
	ABOVE_NIBBLE,
	TEXTURED_HALO,
	COMBINED_ALPHA,
	SATURATED_TRANSPARENCY,
	FOG_BYPASS,
	BEHIND_ROAD,
	CLOSE_HALO,
	FIXED_Z_NEAR,
	FIXED_Z_FAR,
	OVERLAP,
	AFTER_PARTICLES,
	HALO_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
	{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

static int
capture_matches(const char *path, const double expected[3])
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int channel;
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	pixel = (const unsigned char *)rgba->pixels + 120 * rgba->pitch +
		200 * 4;
	success = 1;

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr, "channel %u: %u expected %.6f\n",
				channel, pixel[channel], expected[channel]);
			success = 0;
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
	unsigned char pixels[32 * 32 * 4];
	unsigned char formats[2] = {0, MH_TEXTURE_ARGB4444};

	const unsigned char source[4] = {128, 64, 32, 85};

	const double cue[3] = {127, 127, 127};
	float raw[3][18], color, depth;
	double expected[3], opacity, hue;
	unsigned int peak, channel;
	unsigned int i, j, m, test, texture;
	int success;

	if (argc != 2)
		return 2;

	memset(meshes, 0, sizeof(meshes));
	memset(vertices, 0, sizeof(vertices));
	memset(draws, 0, sizeof(draws));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;

	textures[1].width = textures[1].height = 32;
	textures[1].rgba = pixels;

	for (i = 0; i < 32 * 32; i++)
		memcpy(pixels + i * 4, source, 4);

	for (m = 0; m < 3; m++) {
		meshes[m].vertices = vertices[m];
		meshes[m].vertex_count = 6;
		meshes[m].material_rgb = raw[m];
		meshes[m].material_rgb_count = 6;
		meshes[m].draws = &draws[m];
		meshes[m].draw_count = 1;
		meshes[m].textures = textures;
		meshes[m].texture_count = 2;
		meshes[m].texture_formats = formats;
		meshes[m].texture_format_count = 2;
		draws[m].count = 6;
		draws[m].prelit = 1;
		list[m] = &meshes[m];

		for (i = 0; i < 6; i++) {
			vertices[m][i].position[0] =
				xy[i][0] * (m == 0 ? 32 : 1);
			vertices[m][i].position[1] =
				xy[i][1] * (m == 0 ? 24 : 1);
			vertices[m][i].position[2] = m == 0 ? -32 : .5f;
			vertices[m][i].uv[0] = vertices[m][i].uv[1] = .5f;

			for (j = 0; j < 3; j++)
				raw[m][i * 3 + j] = m == 0 ? 127 : 0;
		}
	}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	gpu = mh_gpu_powervr_open(NULL, list, 3, 320, 240, 0);
	success = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 300,
			&projection, NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 300, &fog, NULL) == MH_ERROR_NONE;

	for (test = 0; test < HALO_CASES && success; test++) {
		color = 32.9f;
		depth = 16;
		texture = 0;
		opacity = 3.0 / 16;

		switch (test) {
		case FULL_HALO:
			color = 255;
			opacity = 1;
			break;

		case BLACK_HALO:
			color = 0;
			opacity = 0;
			break;

		case BELOW_NIBBLE:
			color = 15.999f;
			opacity = 0;
			break;

		case ABOVE_NIBBLE:
			color = 16.001f;
			opacity = 2.0 / 16;
			break;

		case TEXTURED_HALO:
			color = 255;
			texture = 1;
			opacity = 6.0 / 16;
			break;

		case COMBINED_ALPHA:
			color = 208;
			texture = 1;
			opacity = 4.0 / 16;
			break;

		case SATURATED_TRANSPARENCY:
			texture = 1;
			opacity = 0;
			break;

		case FOG_BYPASS:
			depth = 96;
			break;

		case BEHIND_ROAD:
			depth = 32.05f;
			opacity = 0;
			break;

		case CLOSE_HALO:
			depth = .75f;
			break;

		default:
			break;
		}

		for (j = 0; j < 3; j++) {
			peak = (unsigned int)color;
			channel = (unsigned int)(color / (1U << j));
			hue = peak == 0 ? 0 : (channel * 255 / peak) / 255.0;
			hue *= texture == 0 ? 255 : (source[j] >> 4) * 17;
			expected[j] = test == AFTER_PARTICLES
				? (127 * (14.0 / 16) + 255 * (2.0 / 16)) *
						(1 - opacity) +
					hue * opacity
				: 127 * (1 - opacity) + hue * opacity;

			if (test == OVERLAP)
				expected[j] = expected[j] * (1 - opacity) +
					hue * opacity;
		}

		for (m = 1; m < 3; m++) {
			draws[m].texture = texture;

			for (i = 0; i < 6; i++) {
				vertices[m][i].position[2] =
					test == FIXED_Z_NEAR  ? 0
					: test == FIXED_Z_FAR ? 1
							: .5f;
				vertices[m][i].projection_w =
					m == 1 ? depth : 24;

				/*
				 * Only the first original corner supplies flat
				 * intensity.
				 */
				for (j = 0; j < 3; j++)
					raw[m][i * 3 + j] = i == 0
						? (test == AFTER_PARTICLES &&
									m == 2
								? 3
								: color /
									(1U << j))
						: 255;
			}
		}

		/*
		 * A distant road lets the fog-bypass halo remain visible.
		 */
		for (i = 0; i < 6; i++) {
			vertices[0][i].position[0] =
				xy[i][0] * (test == FOG_BYPASS ? 128 : 32);
			vertices[0][i].position[1] =
				xy[i][1] * (test == FOG_BYPASS ? 96 : 24);
			vertices[0][i].position[2] =
				test == FOG_BYPASS ? -128 : -32;
		}

		success = mh_gpu_renderer_set_fog(
				gpu, test == FOG_BYPASS ? &fog : NULL) &&
			mh_gpu_renderer_mesh_lit(gpu, 0, vertices[0]) &&
			mh_gpu_renderer_mesh_halos(gpu, 1) &&
			(test == AFTER_PARTICLES
					? mh_gpu_renderer_mesh_particles(gpu, 2)
					: mh_gpu_renderer_mesh_halos(gpu, 2)) &&
			mh_gpu_renderer_mesh_visible(gpu, 2,
				test == OVERLAP || test == AFTER_PARTICLES) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 1, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!success)
			fprintf(stderr, "PowerVR halo case %u: %s\n", test,
				SDL_GetError());
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (success)
		puts("Fifteen PowerVR halo frames: normalized flat hue, fractional thresholds, texture/primitive transparency, fog bypass, reciprocal depth, overlap and post-particle ordering pass");

	return !success;
}
