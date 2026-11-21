#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

/* Each 8x8 capture follows Original/Nearest/Bilinear/Trilinear row order. */
#define SAMPLE_COUNT 64

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
sample_frame(struct mh_renderer *gpu, const char *path,
	unsigned char pixels[SAMPLE_COUNT],
	const struct mh_projection *projection)
{
	SDL_Surface *bmp, *rgba;
	unsigned int i;
	int ok;

	ok = projection != NULL
		? mh_renderer_scene_frame_lens(gpu, projection, identity, NULL,
			1, NULL, NULL, NULL, NULL) == MH_ERROR_NONE
		: mh_renderer_scene_frame(gpu, identity, identity, NULL, 1);

	if (!ok || !mh_renderer_capture(gpu, path))
		return 0;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	for (i = 0; i < SAMPLE_COUNT; i++)
		pixels[i] = *((const unsigned char *)rgba->pixels +
			(110 + i / 8) * rgba->pitch + (150 + i % 8) * 4);

	SDL_DestroySurface(rgba);

	return 1;
}

static int
check_palette_filters(const struct mh_mesh *mesh, const char *path,
	const unsigned char original[SAMPLE_COUNT])
{
	struct mh_mesh palette_mesh;
	const struct mh_mesh *input;
	struct mh_renderer *gpu;
	unsigned char format, pixels[SAMPLE_COUNT];
	unsigned int i;
	int ok;

	/* The same checkerboard now selects black, not an averaged gray. */
	format = MH_TEXTURE_PALETTE_RGB;
	palette_mesh = *mesh;
	palette_mesh.texture_formats = &format;
	palette_mesh.texture_format_count = 1;
	input = &palette_mesh;
	gpu = mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);
	ok = gpu != NULL && sample_frame(gpu, path, pixels, NULL) &&
		memcmp(pixels, original, SAMPLE_COUNT) == 0 &&
		mh_renderer_set_texture_filter(gpu, MH_TEXTURE_FILTER_TRILINEAR,
			NULL) == MH_ERROR_NONE &&
		sample_frame(gpu, path, pixels, NULL);

	for (i = 0; i < SAMPLE_COUNT && ok; i++)
		ok = pixels[i] == 0;

	mh_renderer_destroy(gpu);

	return ok;
}

static int
check_direct3d_filters(const struct mh_mesh *mesh, const char *path)
{
	struct mh_mesh prepared;
	const struct mh_mesh *input;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_projection projection;
	struct mh_renderer *gpu;
	unsigned char captured[4][SAMPLE_COUNT], again[SAMPLE_COUNT], group;
	float raw[18];
	unsigned int i, channel, filter;
	int ok;

	prepared = *mesh;
	draw = mesh->draws[0];
	group = 1;
	prepared.vertices = vertices;
	prepared.draws = &draw;
	prepared.draw_groups = &group;
	prepared.material_rgb = raw;
	prepared.material_rgb_count = 6;

	for (i = 0; i < 6; i++) {
		vertices[i] = mesh->vertices[i];
		vertices[i].position[0] *= 10;
		vertices[i].position[1] *= 7.5f;
		vertices[i].position[2] = -10;

		for (channel = 0; channel < 3; channel++)
			raw[i * 3 + channel] = 255;
	}

	input = &prepared;
	gpu = mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);
	ok = gpu != NULL && mh_renderer_set_fog(gpu, NULL) &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
			&projection, NULL) == MH_ERROR_NONE;

	for (filter = 0; filter < 4 && ok; filter++)
		ok = mh_renderer_set_texture_filter(gpu,
			(enum mh_texture_filter)filter,
			NULL) == MH_ERROR_NONE &&
			sample_frame(gpu, path, captured[filter], &projection);

	if (ok) {
		ok = memcmp(captured[0], captured[2], SAMPLE_COUNT) == 0 &&
			memcmp(captured[1], captured[2], SAMPLE_COUNT) != 0 &&
			memcmp(captured[2], captured[3], SAMPLE_COUNT) != 0;

		for (i = 0; i < SAMPLE_COUNT && ok; i++)
			ok = captured[3][i] == 127 &&
				(captured[1][i] == 0 || captured[1][i] == 255);
	}

	/* The alpha pass overrides even trilinear with base-level nearest. */
	draw.alpha_test = 1;
	ok = ok && sample_frame(gpu, path, again, &projection) &&
		memcmp(again, captured[1], SAMPLE_COUNT) == 0;
	draw.alpha_test = 0;
	ok = ok &&
		mh_renderer_set_texture_filter(gpu, MH_TEXTURE_FILTER_ORIGINAL,
			NULL) == MH_ERROR_NONE &&
		sample_frame(gpu, path, again, &projection) &&
		memcmp(again, captured[0], SAMPLE_COUNT) == 0;
	mh_renderer_destroy(gpu);

	if (ok)
		puts("Direct3D: six minified texture frames pass nearest/bilinear/mip selection, alpha override and restoration");

	return ok;
}

int
main(int argc, char *argv[])
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	struct mh_image image;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh mesh;
	struct mh_renderer *gpu;
	const struct mh_mesh *input;
	unsigned char captured[4][SAMPLE_COUNT], again[SAMPLE_COUNT];
	unsigned int x, y, channel, i, filter;
	size_t pixel;
	int ok;

	if (argc != 2)
		return 2;

	/* Five source texels per output pixel: mip levels contain exact 127. */
	image.width = 1280;
	image.height = 960;
	image.rgba =
		(unsigned char *)malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return 1;

	for (y = 0; y < image.height; y++) {
		for (x = 0; x < image.width; x++) {
			pixel = ((size_t)y * image.width + x) * 4;

			for (channel = 0; channel < 3; channel++)
				image.rgba[pixel + channel] =
					(x + y) % 2 ? 255 : 0;

			image.rgba[pixel + 3] = 255;
		}
	}

	memset(vertices, 0, sizeof(vertices));
	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = .5f;
		vertices[i].uv[0] =
			(xy[i][0] + .8f) / 1.6f + .25f / image.width;
		vertices[i].uv[1] = (xy[i][1] + .8f) / 1.6f;

		for (channel = 0; channel < 3; channel++)
			vertices[i].color[channel] = 1;
	}

	draw.count = 6;
	draw.prelit = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &image;
	mesh.texture_count = 1;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		free(image.rgba);
		return 1;
	}

	input = &mesh;
	gpu = mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);
	ok = gpu != NULL;

	for (filter = 0; filter < 4 && ok; filter++) {
		ok = mh_renderer_set_texture_filter(gpu,
			(enum mh_texture_filter)filter,
			NULL) == MH_ERROR_NONE &&
			sample_frame(gpu, argv[1], captured[filter], NULL);
	}

	if (ok) {
		ok = memcmp(captured[0], captured[2], SAMPLE_COUNT) == 0 &&
			memcmp(captured[1], captured[2], SAMPLE_COUNT) != 0 &&
			memcmp(captured[2], captured[3], SAMPLE_COUNT) != 0;

		for (i = 0; i < SAMPLE_COUNT && ok; i++)
			ok = captured[3][i] == 127 &&
				(captured[1][i] == 0 || captured[1][i] == 255);
	}

	if (ok)
		ok = mh_renderer_set_texture_filter(gpu,
			(enum mh_texture_filter)99,
			NULL) == MH_ERROR_INVALID &&
			sample_frame(gpu, argv[1], again, NULL) &&
			memcmp(again, captured[3], SAMPLE_COUNT) == 0;

	if (ok)
		ok = mh_renderer_set_texture_filter(gpu,
			MH_TEXTURE_FILTER_ORIGINAL,
			NULL) == MH_ERROR_NONE &&
			sample_frame(gpu, argv[1], again, NULL) &&
			memcmp(again, captured[0], SAMPLE_COUNT) == 0;

	mh_renderer_destroy(gpu);

	if (ok)
		ok = check_palette_filters(&mesh, argv[1], captured[0]);

	if (ok)
		ok = check_direct3d_filters(&mesh, argv[1]);

	if (!ok)
		fprintf(stderr, "Texture filter frame failed: %s\n",
			SDL_GetError());

	SDL_Quit();
	free(image.rgba);

	if (ok)
		puts("Texture filtering: nearest/bilinear/trilinear pixels, palette mip selection, Original restoration and rejected choice passed");

	return ok ? 0 : 1;
}
