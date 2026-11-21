#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

enum sky_case {
	SOLID_SKY,
	TEXTURED_SKY,
	PADDED_U,
	REPEATED_U,
	FAR_SKY,
	NEAR_SKY,
	FAR_SKY_NEAR_Z,
	NEAR_SKY_NEAR_Z,
	SKY_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

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
		160 * 4;
	success = 1;

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr, "channel %u: %u expected %.5f\n",
				channel, pixel[channel], expected[channel]);
			success = 0;
		}

	SDL_DestroySurface(rgba);

	return success;
}

int
main(int argc, char *argv[])
{
	static const float xy[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

	static const float triangle[3][2] = {
		{-.8f, -.8f}, {.8f, -.8f}, {0, .8f}};

	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char page[32 * 64 * 4];
	unsigned char formats[2] = {0, MH_TEXTURE_RGB555};
	struct mh_image textures[2];
	struct mh_vertex sky[6], vertices[3];
	struct mh_mesh meshes[2];
	const struct mh_mesh *list[2];
	struct mh_draw draws[2];
	struct mh_mesh_primitive primitive;
	struct mh_gpu_renderer *gpu;
	struct mh_projection projection;
	float raw[9];
	double expected[3];
	unsigned int i, channel, test;
	int success;

	if (argc != 2)
		return 2;

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	memset(sky, 0, sizeof(sky));
	memset(vertices, 0, sizeof(vertices));
	memset(&primitive, 0, sizeof(primitive));

	for (i = 0; i < 32 * 64; i++) {
		page[i * 4] = 128;
		page[i * 4 + 1] = 192;
		page[i * 4 + 2] = page[i * 4 + 3] = 255;
	}

	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1].width = 32;
	textures[1].height = 64;
	textures[1].rgba = page;

	for (i = 0; i < 2; i++) {
		list[i] = &meshes[i];
		meshes[i].textures = textures;
		meshes[i].texture_count = 2;
		meshes[i].texture_formats = formats;
		meshes[i].texture_format_count = 2;
		meshes[i].draws = &draws[i];
		meshes[i].draw_count = 1;
		draws[i].prelit = 1;
	}

	meshes[0].vertices = sky;
	meshes[0].vertex_count = draws[0].count = 6;
	meshes[1].vertices = vertices;
	meshes[1].vertex_count = draws[1].count = primitive.count = 3;
	meshes[1].primitives = &primitive;
	meshes[1].primitive_count = 1;
	meshes[1].material_rgb = raw;
	meshes[1].material_rgb_count = 3;
	primitive.type = 4;

	for (i = 0; i < 3; i++) {
		vertices[i].position[0] = triangle[i][0] * 32;
		vertices[i].position[1] = triangle[i][1] * 32;
		vertices[i].position[2] = -32;
		raw[i * 3] = 180;
		raw[i * 3 + 1] = 100;
		raw[i * 3 + 2] = 60;
	}

	gpu = mh_gpu_powervr_open(NULL, list, 2, 320, 240, 0);
	success = gpu != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 300,
			&projection, NULL) == MH_ERROR_NONE;

	if (success)
		success = mh_gpu_set_blur_history((struct mh_renderer *)gpu, 2,
				NULL) == MH_ERROR_UNSUPPORTED &&
			mh_gpu_set_blur_history((struct mh_renderer *)gpu, 3,
				NULL) == MH_ERROR_UNSUPPORTED;

	for (test = 0; success && test < SKY_CASES; test++) {
		draws[0].texture =
			test >= TEXTURED_SKY && test <= REPEATED_U ? 1 : 0;
		expected[0] = 51;
		expected[1] = 102;
		expected[2] = 153;

		if (test >= TEXTURED_SKY && test <= REPEATED_U) {
			expected[0] = test == PADDED_U ? 0 : 132;
			expected[1] = test == PADDED_U ? 0 : 198;
			expected[2] = test == PADDED_U ? 0 : 255;
		}

		if (test == FAR_SKY || test == FAR_SKY_NEAR_Z) {
			expected[0] = 180;
			expected[1] = 100;
			expected[2] = 60;
		}

		for (i = 0; i < 6; i++) {
			sky[i].position[0] = xy[i][0];
			sky[i].position[1] = xy[i][1];
			sky[i].position[2] =
				test >= FAR_SKY_NEAR_Z ? .1f : .9999f;
			sky[i].projection_w =
				test == NEAR_SKY || test == NEAR_SKY_NEAR_Z
				? .1f
				: 10000;
			sky[i].uv[0] = test == PADDED_U ? 1.5f
				: test == REPEATED_U	? 2.5f
							: .5f;
			sky[i].uv[1] = .5f;

			/* Ordinary SGL sky ignores the source alpha byte. */
			sky[i].opacity = 0;

			for (channel = 0; channel < 3; channel++)
				sky[i].color[channel] = draws[0].texture
					? 1
					: (channel + 1) * .2f;
		}

		success = mh_gpu_renderer_set_motion_blur(gpu, test % 2) &&
			!gpu->motion_blur && gpu->back_color == NULL &&
			gpu->third_color == NULL &&
			mh_gpu_renderer_mesh_visible(gpu, 1, test >= FAR_SKY) &&
			mh_gpu_renderer_mesh_lit(gpu, 1, vertices) &&
			mh_gpu_renderer_mesh_sky(gpu, 0) &&
			mh_gpu_renderer_mesh_sky(gpu, 0) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 0, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!success)
			fprintf(stderr, "PowerVR sky case %u: %s\n", test,
				SDL_GetError());

		/* Upload adjustment must never accumulate into source UVs. */
		for (i = 0; success && i < 6; i++)
			success = sky[i].uv[0] ==
				(test == PADDED_U		     ? 1.5f
						: test == REPEATED_U ? 2.5f
								: .5f);
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (success)
		puts("PowerVR sky: eight frames pass RGB555 padding/repeat, opaque alpha, repeated updates, ignored blur without history allocation and reciprocal-W depth against ordinary geometry");

	return !success;
}
