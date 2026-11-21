#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
capture_matches(const char *path, unsigned int x, unsigned int y,
	const double expected[3])
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

	pixel = (const unsigned char *)rgba->pixels + y * rgba->pitch + x * 4;
	success = 1;

	for (channel = 0; channel < 3; channel++) {
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr,
				"%s pixel %u,%u channel %u: %u expected %.6f\n",
				path, x, y, channel, pixel[channel],
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
	static const float xy[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

	static const float depths[3] = {.1f, .45f, .9f};
	const double cue[3] = {130, 39, 16};
	const double source[3] = {128, 64, 32};
	struct mh_vertex vertices[6];
	struct mh_mesh mesh;
	struct mh_draw draw;
	struct mh_image white;
	struct mh_fog fog;
	struct mh_renderer *renderer;
	unsigned char texel[4] = {255, 255, 255, 255};
	float view[16], models[16], projection[16];
	double expected[3], amount, t;
	char path[1024];
	unsigned int i, channel, test;
	int success;

	if (argc != 2 || strlen(argv[1]) > 900) {
		fprintf(stderr, "Usage: mh-fog-render CAPTURE_PREFIX\n");
		return 2;
	}

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(vertices, 0, sizeof(vertices));
	draw.count = 6;
	draw.prelit = 1;
	white.rgba = texel;
	white.width = white.height = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &white;
	mesh.texture_count = 1;

	for (i = 0; i < 6; i++) {
		memcpy(vertices[i].position, xy[i], sizeof(xy[i]));

		for (channel = 0; channel < 3; channel++)
			vertices[i].color[channel] =
				(float)(source[channel] / 255);
	}

	memcpy(view, identity, sizeof(view));
	view[10] = -180;
	memcpy(models, identity, sizeof(models));
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}

	renderer = mh_sdl_renderer_create(NULL, &mesh);
	success = renderer != NULL &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE;

	if (success)
		success = mh_renderer_set_fog(renderer, &fog);

	/*
	 * Constant-depth surfaces test near, middle and fully fogged geometry.
	 * Then toggle off/on; an opaque lit upload must still receive fog.
	 */
	for (test = 0; test < 5 && success; test++) {
		for (i = 0; i < 6; i++)
			vertices[i].position[2] = depths[test < 3 ? test : 1];

		amount = (255 - mh_fog_visibility(vertices[0].position[2])) /
			255.0;

		if (test == 3) {
			success = mh_renderer_set_fog(renderer, NULL);
			amount = 0;
		}

		if (test == 4)
			success = mh_renderer_set_fog(renderer, &fog);

		if (success)
			success = mh_renderer_mesh_lit(renderer, 0, vertices);

		for (channel = 0; channel < 3; channel++)
			expected[channel] = source[channel] * (1 - amount) +
				cue[channel] * amount;

		sprintf(path, "%s-depth-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);
	}

	/*
	 * Vary depth across a full-screen quad. At x=1/4 the original
	 * interpolates the already-clamped vertex factors: .25, not
	 * fog(depth=.3)=.2.
	 */
	if (success) {
		for (i = 0; i < 6; i++)
			vertices[i].position[2] = xy[i][0] < 0 ? .1f : .9f;

		success = mh_renderer_mesh_lit(renderer, 0, vertices);
		sprintf(path, "%s-interpolated.bmp", argv[1]);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path);

		for (i = 0; i < 3 && success; i++) {
			t = (240 * (i + 1) + .5) / 960.0;

			for (channel = 0; channel < 3; channel++)
				expected[channel] = source[channel] * (1 - t) +
					cue[channel] * t;

			success = capture_matches(
				path, 240 * (i + 1), 320, expected);
		}
	}

	/*
	 * Repeat with unequal clip W. A perspective-correct fog varying would
	 * fail this: the original Glide alpha is linear in screen coordinates.
	 */
	if (success) {
		memcpy(projection, identity, sizeof(projection));
		projection[10] = .5f;
		projection[11] = 1;
		projection[15] = 0;

		for (i = 0; i < 6; i++) {
			vertices[i].position[0] =
				xy[i][0] * vertices[i].position[2];
			vertices[i].position[1] =
				xy[i][1] * vertices[i].position[2];
		}

		sprintf(path, "%s-perspective.bmp", argv[1]);
		success = mh_renderer_mesh_lit(renderer, 0, vertices) &&
			mh_renderer_scene_frame(
				renderer, projection, view, models, 0) &&
			mh_renderer_capture(renderer, path);

		for (i = 0; i < 3 && success; i++) {
			t = (240 * (i + 1) + .5) / 960.0;

			for (channel = 0; channel < 3; channel++)
				expected[channel] = source[channel] * (1 - t) +
					cue[channel] * t;

			success = capture_matches(
				path, 240 * (i + 1), 320, expected);
		}

		for (i = 0; i < 6; i++)
			memcpy(vertices[i].position, xy[i], sizeof(xy[i]));
	}

	/*
	 * A translated model tests use of model-view depth, independently of
	 * clip projection: z=.1 with model +.35 becomes the middle fog
	 * distance.
	 */
	if (success) {
		for (i = 0; i < 6; i++)
			vertices[i].position[2] = .1f;

		models[14] = .35f;
		amount = (255 - mh_fog_visibility(.45f)) / 255.0;

		for (channel = 0; channel < 3; channel++)
			expected[channel] = source[channel] * (1 - amount) +
				cue[channel] * amount;

		sprintf(path, "%s-model.bmp", argv[1]);
		success = mh_renderer_mesh_lit(renderer, 0, vertices) &&
			mh_renderer_scene_frame(
				renderer, identity, view, models, 0) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 480, 320, expected);
		models[14] = 0;
	}

	/*
	 * Effect passes retain their own blend equation with no fog. They draw
	 * over the native fog-colored placeholder background.
	 */
	for (test = 0; test < 3 && success; test++) {
		for (i = 0; i < 6; i++) {
			vertices[i].position[2] = .45f;

			for (channel = 0; channel < 3; channel++)
				vertices[i].color[channel] =
					test == 0 ? 216.0f / 255 : .1f;
		}

		if (test == 0)
			success = mh_renderer_mesh_skids(renderer, 0);
		else if (test == 1)
			success = mh_renderer_mesh_unavailable(
				renderer, 0, vertices);
		else
			success = mh_renderer_mesh_particles(renderer, 0);

		for (channel = 0; channel < 3; channel++)
			expected[channel] = test == 0 ? cue[channel] * 216 / 255
						: cue[channel] + 25.5;

		sprintf(path, "%s-effect-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);
	}

	/*
	 * A decoded zero-alpha texel must not punch holes in ordinary road
	 * materials. The explicitly alpha-enabled draw still discards it.
	 */
	mh_renderer_destroy(renderer);
	renderer = NULL;

	if (success) {
		texel[3] = 0;

		for (i = 0; i < 6; i++) {
			vertices[i].position[2] = .45f;

			for (channel = 0; channel < 3; channel++)
				vertices[i].color[channel] =
					(float)(source[channel] / 255);
		}

		renderer = mh_sdl_renderer_create(NULL, &mesh);
		success =
			renderer != NULL && mh_renderer_set_fog(renderer, &fog);

		for (test = 0; test < 2 && success; test++) {
			draw.alpha_test = (int)test;
			amount = (255 - mh_fog_visibility(.45f)) / 255.0;

			for (channel = 0; channel < 3; channel++)
				expected[channel] = test
					? cue[channel]
					: source[channel] * (1 - amount) +
						cue[channel] * amount;

			sprintf(path, "%s-alpha-%u.bmp", argv[1], test);
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);
		}
	}

	if (!success)
		fprintf(stderr, "Fog render qualification failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("Fog pixels, interpolation, transformed depth, alpha materials and three effect bypasses passed");

	return !success;
}
