#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static struct mh_ui_list ui;

static int
pixel_matches(const SDL_Surface *surface, unsigned int x, unsigned int y,
	unsigned int red, unsigned int green, unsigned int blue)
{
	const unsigned char *pixel;

	pixel = (const unsigned char *)surface->pixels +
		(size_t)y * surface->pitch + x * 4;

	if (abs((int)pixel[0] - (int)red) <= 1 &&
		abs((int)pixel[1] - (int)green) <= 1 &&
		abs((int)pixel[2] - (int)blue) <= 1)
		return 1;

	fprintf(stderr, "viewport pixel %u,%u: %u,%u,%u expected %u,%u,%u\n", x,
		y, pixel[0], pixel[1], pixel[2], red, green, blue);

	return 0;
}

static int
check_capture(struct mh_renderer *renderer, const char *path,
	unsigned int width, unsigned int height, int tron_mode)
{
	SDL_Surface *bmp, *rgba;
	struct mh_file picture;
	const unsigned char *pixel;
	unsigned int x, y, expected;
	int success;

	if (!mh_renderer_capture(renderer, path))
		return 0;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = rgba->w == (int)width && rgba->h == (int)height;

	if (success && tron_mode)
		success =
			pixel_matches(rgba, width / 2, height / 2, 0, 0, 99) &&
			pixel_matches(rgba, 1, 1, 0, 0, 0) &&
			pixel_matches(rgba, width - 1, height - 1, 0, 0, 0);
	else if (success)
		success = pixel_matches(rgba, 0, 0, 255, 0, 0) &&
			pixel_matches(rgba, width / 2, height / 2, 255, 0, 0) &&
			pixel_matches(rgba, width - 1, height - 1, 0, 255, 0) &&
			pixel_matches(rgba, width - 3, height - 1, 255, 0, 0);

	if (success) {
		success = mh_renderer_tga_capture(renderer, &picture);

		if (success) {
			success = picture.size ==
					18 + (size_t)width * height * 2 &&
				picture.data[2] == 2 &&
				picture.data[16] == 16 &&
				picture.data[17] == 0 &&
				mh_read_u16le(picture.data + 12) == width &&
				mh_read_u16le(picture.data + 14) == height;

			for (y = 0; y < height && success; y++)
				for (x = 0; x < width; x++) {
					pixel = (const unsigned char *)
							rgba->pixels +
						(size_t)y * rgba->pitch + x * 4;

					expected = (pixel[0] / 8) * 1024 +
						(pixel[1] / 8) * 32 +
						pixel[2] / 8;

					if (mh_read_u16le(picture.data + 18 +
						((size_t)(height - 1 - y) *
								width +
							x) *
							2) != expected) {
						fprintf(stderr,
							"screenshot pixel %u,%u differs from presented BMP\n",
							x, y);
						success = 0;
						break;
					}
				}

			free(picture.data);
		}
	}

	SDL_DestroySurface(rgba);

	return success;
}

static int
check_viewport(
	const char *path, unsigned int width, unsigned int height, int windowed)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	static const float green[4] = {0, 1, 0, 1};
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh mesh;
	struct mh_image image;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	struct mh_renderer_tron_face face;
	struct mh_water_frame water;
	struct mh_depth_probe probe;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer;
	SDL_Window *window;
	unsigned char white[4] = {255, 255, 255, 255}, visible;
	unsigned int i, c, j;
	float phase;
	int success;

	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&mesh, 0, sizeof(mesh));

	for (i = 0; i < 6; i++) {
		c = corners[i];
		vertices[i].position[0] = c == 0 || c == 3 ? -1 : 1;
		vertices[i].position[1] = c < 2 ? 1 : -1;
		vertices[i].position[2] = .25f;
		vertices[i].color[0] = 1;
	}

	image.width = image.height = 1;
	image.rgba = white;
	draw.count = 6;
	draw.prelit = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &image;
	mesh.texture_count = 1;
	meshes[0] = &mesh;
	memset(&atlas, 0, sizeof(atlas));
	renderer = NULL;
	window = NULL;
	success = mh_ui_atlas_init(8, 8, &atlas, NULL) == MH_ERROR_NONE &&
		mh_ui_atlas_add(&atlas, &image, &region, NULL) == MH_ERROR_NONE;

	if (success && windowed) {
		window = SDL_CreateWindow("Motorhead viewport qualification",
			640, 480, SDL_WINDOW_RESIZABLE);
		success = window != NULL;
	}

	if (success)
		renderer = mh_sdl_renderer_create_scene_sized(
			window, meshes, 1, width, height);

	success = success && renderer != NULL &&
		mh_renderer_ui_upload(renderer, &atlas);
	mh_ui_begin(&ui, width, height);

	if (success)
		success = mh_ui_quad_scaled(&ui, &atlas, &region,
				(float)width - 2, (float)height - 2, 2, 2,
				green) &&
			mh_renderer_scene_frame_ui(
				renderer, identity, identity, NULL, 0, &ui) &&
			check_capture(renderer, path, width, height, 0);

	/*
	 * Window resizing must not change the selected scene/capture
	 * dimensions.
	 */
	if (success && windowed) {
		success = SDL_SetWindowSize(window, 853, 517) &&
			SDL_SyncWindow(window);
		SDL_PumpEvents();
		success = success &&
			mh_renderer_scene_frame_ui(
				renderer, identity, identity, NULL, 0, &ui) &&
			check_capture(renderer, path, width, height, 0);
	}

	/* Software word-text commands must not silently disappear on Glide. */
	if (success) {
		ui.text_count = 1;
		success = !mh_renderer_scene_frame_ui(
				renderer, identity, identity, NULL, 0, &ui) &&
			check_capture(renderer, path, width, height, 0);
		ui.text_count = 0;
	}

	/*
	 * Last pixel must be addressable even past the old 960x640 boundary.
	 * Equal depth is occluded; front depth is visible. UI keeps scene
	 * depth.
	 */
	probe.active = 1;
	probe.x = width - .5f;
	probe.y = height - .5f;
	probe.depth = .25f;
	visible = 1;

	if (success)
		success = mh_renderer_depth_queue(renderer, &probe, 1) &&
			mh_renderer_depth_read(renderer, &visible, 1) &&
			!visible;

	probe.depth = .125f;

	if (success)
		success = mh_renderer_depth_queue(renderer, &probe, 1) &&
			mh_renderer_depth_read(renderer, &visible, 1) &&
			visible;

	probe.x = (float)width;

	if (success)
		success = !mh_renderer_depth_queue(renderer, &probe, 1);

	ui.width++;

	if (success)
		success = !mh_renderer_scene_frame_ui(
			renderer, identity, identity, NULL, 0, &ui);

	ui.width--;
	phase = 0;

	if (success)
		success = mh_water_frame(&phase, (float)width, (float)height,
				&water, NULL) == MH_ERROR_NONE &&
			mh_renderer_set_water(renderer, &water);

	if (success) {
		water.height++;
		success = !mh_renderer_set_water(renderer, &water) &&
			mh_renderer_set_water(renderer, NULL);
	}

	memset(&face, 0, sizeof(face));
	face.pass = MH_SCENE_TEXTURED;
	face.count = 4;
	face.smooth = 1;

	for (i = 0; i < 4; i++) {
		face.corners[i].xy[0] =
			(i == 1 || i == 2 ? .75f : .25f) * width;
		face.corners[i].xy[1] = (i >= 2 ? .75f : .25f) * height;
		face.corners[i].depth = .5f;
		face.corners[i].reciprocal_w = 1;
		face.corners[i].visibility = 255;

		for (j = 0; j < 3; j++)
			face.corners[i].rgb[j] = 100;
	}

	if (success)
		success = mh_renderer_tron_frame_ui(renderer, &face, 1, NULL) &&
			check_capture(renderer, path, width, height, 1);

	if (!success)
		fprintf(stderr, "viewport %ux%u failed: %s\n", width, height,
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_DestroyWindow(window);
	free(atlas.image.rgba);

	return success;
}

int
main(int argc, char *argv[])
{
	static const unsigned int sizes[][2] = {
		{320, 200}, {800, 600}, {854, 481}, {1280, 720}, {4096, 2160}};

	unsigned int i;
	int success, windowed;

	if (argc != 2 && !(argc == 3 && strcmp(argv[2], "--window") == 0))
		return 2;

	windowed = argc == 3;

	if (!windowed)
		SDL_SetHintWithPriority(
			SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	success = mh_sdl_renderer_create_scene_sized(NULL, NULL, 0, 0, 640) ==
			NULL &&
		mh_sdl_renderer_create_scene_sized(NULL, NULL, 0, 960, 0) ==
			NULL &&
		mh_sdl_renderer_create_scene_sized(NULL, NULL, 0, 4097, 2160) ==
			NULL &&
		mh_sdl_renderer_create_scene_sized(NULL, NULL, 0, 4096, 2161) ==
			NULL;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && success; i++)
		success = check_viewport(
			argv[1], sizes[i][0], sizes[i][1], windowed);

	SDL_Quit();

	if (success)
		puts("Five native target sizes passed world/UI pixels, BMP/RGB555 screenshot equivalence, padded capture, depth probes, TRON and distortion dimensions");

	return !success;
}
