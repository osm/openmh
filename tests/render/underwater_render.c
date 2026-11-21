#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static unsigned int width = MH_VIEW_WIDTH, height = MH_VIEW_HEIGHT;

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

enum effect_kind {
	EFFECT_WORLD,
	EFFECT_SKID,
	EFFECT_PARTICLE,
	EFFECT_UNAVAILABLE,
	EFFECT_HALO,
	EFFECT_SHADOW,
	EFFECT_COUNT
};

static int
select_effect(struct mh_renderer *renderer, unsigned int effect,
	struct mh_vertex *vertices)
{
	switch (effect) {
	case EFFECT_WORLD:
		return mh_renderer_update_mesh(renderer, 0);
	case EFFECT_SKID:
		return mh_renderer_mesh_skids(renderer, 0);
	case EFFECT_PARTICLE:
		return mh_renderer_mesh_particles(renderer, 0);
	case EFFECT_UNAVAILABLE:
		return mh_renderer_mesh_unavailable(renderer, 0, vertices);
	case EFFECT_HALO:
		return mh_renderer_mesh_halos(renderer, 0);
	default:
		return mh_renderer_mesh_shadows(renderer, 0);
	}
}

static int
captures_match(const char *first, const char *second)
{
	SDL_Surface *bmp, *first_image, *second_image;
	const unsigned char *first_pixel, *second_pixel;
	unsigned int x, y, channel, differences, edge_differences;

	bmp = SDL_LoadBMP(first);

	if (bmp == NULL)
		return 0;

	first_image = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);
	bmp = SDL_LoadBMP(second);

	if (bmp == NULL) {
		SDL_DestroySurface(first_image);
		return 0;
	}

	second_image = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (first_image == NULL || second_image == NULL ||
		first_image->w != (int)width || first_image->h != (int)height ||
		second_image->w != (int)width ||
		second_image->h != (int)height) {
		SDL_DestroySurface(first_image);
		SDL_DestroySurface(second_image);
		return 0;
	}

	differences = edge_differences = 0;
	first_pixel = (const unsigned char *)first_image->pixels +
		25 * first_image->pitch + 32 * 4;
	second_pixel = (const unsigned char *)second_image->pixels +
		25 * second_image->pitch + 32 * 4;

	if (first_pixel[0] != 255 || first_pixel[1] != 0 ||
		first_pixel[2] != 255 ||
		memcmp(first_pixel, second_pixel, 3) != 0) {
		SDL_DestroySurface(first_image);
		SDL_DestroySurface(second_image);
		fprintf(stderr,
			"underwater wave moved or recolored the UI overlay\n");
		return 0;
	}

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++) {
			first_pixel =
				(const unsigned char *)first_image->pixels +
				y * first_image->pitch + x * 4;
			second_pixel =
				(const unsigned char *)second_image->pixels +
				y * second_image->pitch + x * 4;

			for (channel = 0; channel < 3; channel++)
				if (abs((int)first_pixel[channel] -
					second_pixel[channel]) > 1) {
					differences++;
					break;
				}

			for (channel = 0; channel < 3; channel++)
				if (abs((int)first_pixel[channel] -
					second_pixel[channel]) > 20) {
					edge_differences++;
					break;
				}
		}

	SDL_DestroySurface(first_image);
	SDL_DestroySurface(second_image);

	if (differences > 32)
		fprintf(stderr,
			"wave capture comparison: %u differing pixels (%u at edges)\n",
			differences, edge_differences);

	return differences <= 32;
}

int
main(int argc, char *argv[])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	static const float reference[4][2] = {
		{200, 120}, {700, 150}, {650, 440}, {180, 380}};

	float points[4][2];
	const struct mh_mesh *meshes[1];
	struct mh_mesh mesh;
	struct mh_vertex vertices[6], source[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	struct mh_image images[2];
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	static struct mh_ui_list ui;
	static const float magenta[4] = {1, 0, 1, 1};
	unsigned char pixels[8 * 8 * 4], white[4] = {255, 255, 255, 255};
	struct mh_renderer *renderer;
	struct mh_water_frame water;
	float phase, xy[2], value;
	unsigned int i, j, c, effect, step;
	char first[1024], second[1024];
	int success;
	unsigned long size;
	char *end;

	if ((argc != 2 && argc != 4) || strlen(argv[1]) > 900)
		return 2;

	if (argc == 4) {
		size = strtoul(argv[2], &end, 10);

		if (*end != 0 || size < MH_VIEW_MIN_WIDTH ||
			size > MH_VIEW_MAX_WIDTH)
			return 2;

		width = (unsigned int)size;
		size = strtoul(argv[3], &end, 10);

		if (*end != 0 || size < MH_VIEW_MIN_HEIGHT ||
			size > MH_VIEW_MAX_HEIGHT)
			return 2;

		height = (unsigned int)size;
	}

	for (i = 0; i < 4; i++) {
		points[i][0] = reference[i][0] * width / 960;
		points[i][1] = reference[i][1] * height / 640;
	}

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(vertices, 0, sizeof(vertices));
	memset(&primitive, 0, sizeof(primitive));
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = images;
	mesh.texture_count = 2;
	mesh.environment_texture = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	primitive.count = 6;
	draw.count = 6;
	draw.prelit = 1;
	draw.environment = 1;
	draw.texture = 1;
	images[0].width = images[0].height = 1;
	images[0].rgba = white;
	images[1].width = images[1].height = 8;
	images[1].rgba = pixels;

	for (i = 0; i < 64; i++) {
		pixels[i * 4] = (unsigned char)((i % 8) * 32);
		pixels[i * 4 + 1] = (unsigned char)((i / 8) * 32);
		pixels[i * 4 + 2] = 96;
		pixels[i * 4 + 3] = 255;
	}

	for (i = 0; i < 6; i++) {
		c = corners[i];
		vertices[i].position[0] = points[c][0] * 2 / width - 1;
		vertices[i].position[1] = 1 - points[c][1] * 2 / height;
		vertices[i].position[2] = .4f;
		vertices[i].projection_w = (float)(c + 1);
		vertices[i].normal[0] = .2f;
		vertices[i].normal[1] = -.3f;

		for (j = 0; j < 3; j++)
			vertices[i].color[j] = (float)(2 + c + j) / 8;

		for (j = 0; j < 2; j++) {
			value = vertices[i].position[j] *
				(j == 0 ? 1 : (float)height / width);
			vertices[i].uv[j] =
				.5f + .2495f * (vertices[i].normal[j] + value);
		}
	}

	memcpy(source, vertices, sizeof(source));
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	meshes[0] = &mesh;
	renderer = mh_sdl_renderer_create_scene_sized(
		NULL, meshes, 1, width, height);
	success = renderer != NULL;
	memset(&atlas, 0, sizeof(atlas));
	atlas.image = images[0];
	region.x = region.y = 0;
	region.width = region.height = 1;
	mh_ui_begin(&ui, width, height);

	if (success)
		success = mh_renderer_ui_upload(renderer, &atlas) &&
			mh_ui_quad_scaled(
				&ui, &atlas, &region, 20, 20, 80, 16, magenta);

	phase = 0;

	for (effect = 0; success && effect < EFFECT_COUNT; effect++)
		for (step = 0; success && step < 4; step++) {
			phase = step == 0   ? 0
				: step == 1 ? 1.5f
				: step == 2 ? 4
					: 6.25f;
			success = mh_water_frame(&phase, width, height, &water,
					NULL) == MH_ERROR_NONE;

			memcpy(vertices, source, sizeof(vertices));
			draw.environment = 1;

			if (success)
				success = select_effect(
					renderer, effect, vertices);

			sprintf(first, "%s-%u-%u-gpu.bmp", argv[1], effect,
				step);

			if (success)
				success = mh_renderer_set_water(
						renderer, &water) &&
					mh_renderer_scene_frame_ui(renderer,
						identity, identity, NULL, 1,
						&ui) &&
					mh_renderer_capture(renderer, first);

			for (i = 0; i < 6 && success; i++) {
				xy[0] = points[corners[i]][0];
				xy[1] = points[corners[i]][1];
				success = mh_water_point(&water, xy, xy,
						NULL) == MH_ERROR_NONE;
				vertices[i].position[0] = xy[0] * 2 / width - 1;
				vertices[i].position[1] =
					1 - xy[1] * 2 / height;
			}

			draw.environment = 0;
			sprintf(second, "%s-%u-%u-cpu.bmp", argv[1], effect,
				step);

			if (success)
				success = select_effect(
						renderer, effect, vertices) &&
					mh_renderer_set_water(renderer, NULL) &&
					mh_renderer_scene_frame_ui(renderer,
						identity, identity, NULL, 0,
						&ui) &&
					mh_renderer_capture(renderer, second) &&
					captures_match(first, second);
		}

	if (!success)
		fprintf(stderr, "Underwater render qualification failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("24 underwater world/effect comparisons: phase wrap, unchanged Z/W/colors, UV-before-wave, UI bypass and CPU projection passed");

	return !success;
}
