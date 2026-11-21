#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
check_picture(struct mh_renderer *renderer, const char *path)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
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

	pixel = (const unsigned char *)rgba->pixels + 120 * rgba->pitch +
		160 * 4;

	/* These RGB values cannot survive a 16-bit framebuffer round trip. */
	success = rgba->w == 320 && rgba->h == 240 && pixel[0] == 17 &&
		pixel[1] == 29 && pixel[2] == 43;
	SDL_DestroySurface(rgba);

	return success;
}

int
main(int argc, char *argv[])
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	static const unsigned int queues[] = {2, 1, 3, 2};
	unsigned char white[4] = {255, 255, 255, 255};
	struct mh_image image;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh mesh;
	const struct mh_mesh *input;
	struct mh_renderer *renderer;
	SDL_Window *window;
	SDL_Event event;
	unsigned int i, queue, frame;
	int success;

	if (argc != 2 || !SDL_Init(SDL_INIT_VIDEO))
		return 2;

	image.width = image.height = 1;
	image.rgba = white;
	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(vertices, 0, sizeof(vertices));

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = .5f;
		vertices[i].color[0] = 17.0f / 255.0f;
		vertices[i].color[1] = 29.0f / 255.0f;
		vertices[i].color[2] = 43.0f / 255.0f;
	}

	draw.count = 6;
	draw.prelit = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &image;
	mesh.texture_count = 1;
	input = &mesh;
	window = SDL_CreateWindow(
		"Presentation check", 320, 240, SDL_WINDOW_RESIZABLE);
	renderer = window == NULL ? NULL
				: mh_sdl_renderer_create_scene_sized(
					window, &input, 1, 320, 240);
	success = renderer != NULL;

	for (queue = 0; success && queue < sizeof(queues) / sizeof(queues[0]);
		queue++) {
		success = mh_renderer_set_frame_queue(renderer, queues[queue],
				NULL) == MH_ERROR_NONE;

		if (success)
			success = mh_renderer_set_vsync(renderer, queue % 2,
					NULL) == MH_ERROR_NONE;

		if (success)
			success = SDL_SetWindowSize(window,
				queue % 2 ? 640 : 320, queue % 2 ? 400 : 240);

		for (frame = 0; success && frame < 8; frame++) {
			SDL_Delay(10);

			while (SDL_PollEvent(&event)) {
			}

			success = mh_renderer_scene_frame(
				renderer, identity, identity, NULL, 1);
		}

		if (success)
			success = check_picture(renderer, argv[1]);
	}

	if (success)
		success = mh_renderer_set_frame_queue(renderer, 0, NULL) ==
				MH_ERROR_INVALID &&
			mh_renderer_set_frame_queue(renderer, 4, NULL) ==
				MH_ERROR_INVALID &&
			check_picture(renderer, argv[1]);

	if (!success)
		fprintf(stderr, "Presentation check failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_DestroyWindow(window);
	renderer =
		mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);

	if (renderer == NULL ||
		mh_renderer_set_vsync(renderer, 0, NULL) !=
			MH_ERROR_UNSUPPORTED ||
		mh_renderer_set_frame_queue(renderer, 2, NULL) !=
			MH_ERROR_UNSUPPORTED)
		success = 0;

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("Presentation: VSync off/on, queue 1/2/3, resize, 32-bit color/readback and headless rejection passed");

	return success ? 0 : 1;
}
