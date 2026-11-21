#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const unsigned int sample_x[5] = {40, 120, 200, 280, 160};
static const unsigned int sample_y[5] = {100, 100, 100, 100, 40};

enum history_mode {
	HISTORY_CAPTURE,
	HISTORY_QUEUE,
	HISTORY_CHANGE
};

static void
make_quad(struct mh_vertex v[6], float left, float right, float depth,
	const double rgb[3])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i, j, c;

	memset(v, 0, 6 * sizeof(*v));

	for (i = 0; i < 6; i++) {
		c = corners[i];
		v[i].position[0] = c == 0 || c == 3 ? left : right;
		v[i].position[1] = c < 2 ? 1 : -1;
		v[i].position[2] = depth;
		v[i].uv[0] = v[i].uv[1] = .5f;

		for (j = 0; j < 3; j++)
			v[i].color[j] = (float)(rgb[j] / 255);
	}
}

static int
capture_matches(const char *path, const double expected[5][3])
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int i, channel;
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = rgba->w == 320 && rgba->h == 200;

	for (i = 0; i < 5 && success; i++) {
		pixel = (const unsigned char *)rgba->pixels +
			sample_y[i] * rgba->pitch + sample_x[i] * 4;

		for (channel = 0; channel < 3; channel++)
			if (fabs(pixel[channel] - expected[i][channel]) > 1.1) {
				fprintf(stderr,
					"%s pixel %u,%u channel %u: %u expected %.4f\n",
					path, sample_x[i], sample_y[i], channel,
					pixel[channel], expected[i][channel]);
				success = 0;
			}

		if (pixel[3] != 255)
			success = 0;
	}

	SDL_DestroySurface(rgba);

	return success;
}

static int
check_matrix_history(const char *prefix, unsigned int mode)
{
	struct mh_vertex v[3][6];
	struct mh_mesh meshes[3];
	const struct mh_mesh *pointers[3];
	struct mh_draw draws[3];
	struct mh_image image;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	static struct mh_ui_list ui;
	struct mh_fog fog;
	struct mh_renderer *renderer;
	const double wall[3] = {200, 40, 20}, cue[3] = {130, 39, 16};
	const float hud[4] = {0, 1, 0, 1};
	unsigned char white[4] = {255, 255, 255, 0};
	double expected[12][5][3], sky[3], old, value, fade;
	float view[16];
	char path[1024];
	unsigned int frame, i, j, stripe;
	int success, enabled, special, hud_on;

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	memset(&atlas, 0, sizeof(atlas));
	memset(expected, 0, sizeof(expected));
	image.width = image.height = 1;
	image.rgba = white;

	for (i = 0; i < 3; i++) {
		meshes[i].vertices = v[i];
		meshes[i].vertex_count = 6;
		meshes[i].draws = &draws[i];
		meshes[i].draw_count = 1;
		meshes[i].textures = &image;
		meshes[i].texture_count = 1;
		draws[i].count = 6;
		draws[i].prelit = 1;
		pointers[i] = &meshes[i];
		make_quad(v[i], -1, 1, .1f, wall);
	}

	/* Sky texture alpha zero must not change the constant blend factor. */
	atlas.image = image;
	memcpy(view, identity, sizeof(view));
	view[10] = -180;
	region.x = region.y = 0;
	region.width = region.height = 1;
	renderer =
		mh_sdl_renderer_create_scene_sized(NULL, pointers, 3, 320, 200);

	/* Opaque HUD image; scene textures were uploaded with alpha zero. */
	white[3] = 255;
	success = renderer != NULL && mh_renderer_mesh_sky(renderer, 0) &&
		mh_renderer_ui_upload(renderer, &atlas) &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(renderer, &fog) &&
		!mh_renderer_set_motion_blur(NULL, 1) &&
		!mh_renderer_set_motion_blur(renderer, 2);
	enabled = mode == 1 || mode == 3;

	if (success && enabled)
		success = mh_renderer_set_motion_blur(renderer, 1);

	for (frame = 0; frame < 12 && success; frame++) {
		if (mode == 2 && frame == 2)
			enabled = 1;

		if (mode != 0 && frame == 4)
			enabled = 0;

		if (mode != 0 && frame == 6)
			enabled = 1;

		/* Repeated setter calls must not discard history. */
		success = mh_renderer_set_motion_blur(renderer, enabled) &&
			!mh_renderer_set_motion_blur(renderer, -1);
		special = frame == 5 || frame == 9;
		stripe = frame % 3;
		hud_on = frame % 2 == 0 || special;
		sky[0] = 20 + frame * 13;
		sky[1] = 150 - frame * 7;
		sky[2] = 30 + frame * 5;
		make_quad(v[0], -1, 1, .99f, sky);
		make_quad(v[1], -1 + stripe * .5f, -.5f + stripe * .5f, .1f,
			wall);
		make_quad(v[2], .5f, 1, .9f, wall);
		mh_ui_begin(&ui, 320, 200);

		if (hud_on)
			success = success &&
				mh_ui_quad_scaled(&ui, &atlas, &region, 140, 20,
					40, 40, hud);

		if (success)
			success = mh_renderer_mesh_sky(renderer, 0) &&
				mh_renderer_update_mesh(renderer, 1) &&
				mh_renderer_update_mesh(renderer, 2);

		if (success) {
			if (frame == 5)
				success = mh_renderer_ui_frame(renderer, &ui);
			else if (frame == 9)
				success = mh_renderer_tron_frame_ui(
					renderer, NULL, 0, &ui);
			else
				success = mh_renderer_scene_frame_ui(
					renderer, identity, view, NULL, 0, &ui);
		}

		/*
		 * The target contains frame N-2, including its former
		 * foreground and HUD. A first late enable has no second target
		 * yet: black.
		 */
		fade = mh_fog_background(.9f) / 255.0;

		for (i = 0; i < 5; i++)
			for (j = 0; j < 3; j++) {
				old = frame >= 2 && !(mode == 2 && frame == 2)
					? expected[frame - 2][i][j]
					: 0;

				value = special ? 0
					: enabled
					? floor((sky[j] * 128 + old * 127) /
							255 +
						.5)
					: sky[j];

				if (!special && sample_x[i] >= stripe * 80 &&
					sample_x[i] < (stripe + 1) * 80)
					value = wall[j];

				if (!special && i == 3)
					value = floor(cue[j] * (1 - fade) +
						value * fade + .5);

				if (hud_on && i == 4)
					value = j == 1 ? 255 : 0;

				expected[frame][i][j] = value;
			}

		sprintf(path, "%s-%u-%02u.bmp", prefix, mode, frame);

		/*
		 * Mode 3 queues the sequence without readback between frames.
		 */
		if (success && (mode != 3 || frame == 11))
			success = mh_renderer_capture(renderer, path) &&
				capture_matches(path,
					(const double (*)[3])expected[frame]);
	}

	mh_renderer_destroy(renderer);

	return success;
}

/* Lens submission preserves diffuse alpha and screen-linear gradients. */
static int
check_lens_history(
	const char *prefix, enum history_mode mode, unsigned int history)
{
	struct mh_vertex vertices[6];
	struct mh_mesh mesh;
	const struct mh_mesh *mesh_pointer;
	struct mh_draw draw;
	struct mh_image image;
	struct mh_projection projection;
	struct mh_renderer *renderer;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	static struct mh_ui_list ui;
	const float hud[4] = {0, 1, 0, 1};
	unsigned char white[4] = {255, 255, 255, 255};
	double expected[12][5][3], sky[3], alpha, old, value;
	char path[1024];
	unsigned int frame, i, j, history_start;
	int success, enabled, special, hud_on;

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(&atlas, 0, sizeof(atlas));
	memset(expected, 0, sizeof(expected));
	image.width = image.height = 1;
	image.rgba = white;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &image;
	mesh.texture_count = 1;
	draw.count = 6;
	draw.prelit = 1;
	sky[0] = sky[1] = sky[2] = 0;
	make_quad(vertices, -1, 1, .9999f, sky);
	atlas.image = image;
	region.x = region.y = 0;
	region.width = region.height = 1;
	mesh_pointer = &mesh;
	renderer = mh_sdl_renderer_create_scene_sized(
		NULL, &mesh_pointer, 1, 320, 200);
	success = renderer != NULL && mh_renderer_ui_upload(renderer, &atlas) &&
		mh_projection_setup(320, 200, 1.6f, 1, .5f, 180, &projection,
			NULL) == MH_ERROR_NONE;

	history_start = 0;

	for (frame = 0; frame < 12 && success; frame++) {
		if (mode == HISTORY_CHANGE && (frame == 4 || frame == 8)) {
			history = frame == 4 ? 2 : 3;
			history_start = frame;
		}

		enabled = frame != 4 && frame != 5;
		special = frame == 5 || frame == 9;
		hud_on = frame % 2 == 0 || special;
		sky[0] = 20 + frame * 13;
		sky[1] = 150 - frame * 7;
		sky[2] = 30 + frame * 5;
		make_quad(vertices, -1, 1, .9999f, sky);

		for (i = 0; i < 6; i++) {
			vertices[i].projection_w =
				vertices[i].position[1] > 0 ? .1f : .9f;
			vertices[i].opacity = frame % 4 == 0  ? 127.0f / 255
				: frame % 4 == 1	      ? 0
				: frame % 4 == 2	      ? 1
				: vertices[i].position[1] > 0 ? 0
							: 1;
		}

		mh_ui_begin(&ui, 320, 200);

		if (hud_on)
			success = mh_ui_quad_scaled(
				&ui, &atlas, &region, 140, 20, 40, 40, hud);

		success = success &&
			mh_renderer_set_blur_history(renderer, history, NULL) ==
				MH_ERROR_NONE &&
			mh_renderer_set_blur_history(renderer, 1, NULL) ==
				MH_ERROR_INVALID &&
			mh_renderer_set_blur_history(renderer, 4, NULL) ==
				MH_ERROR_INVALID &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_set_motion_blur(renderer, enabled);

		if (success) {
			if (frame == 5)
				success = mh_renderer_ui_frame(renderer, &ui);
			else if (frame == 9)
				success = mh_renderer_tron_frame_ui(
					renderer, NULL, 0, &ui);
			else
				success = mh_renderer_scene_frame_lens(renderer,
						&projection, identity, NULL,
						0, &ui, NULL, NULL,
						NULL) == MH_ERROR_NONE;
		}

		for (i = 0; i < 5; i++)
			for (j = 0; j < 3; j++) {
				alpha = frame % 4 == 0	 ? 127.0 / 255
					: frame % 4 == 1 ? 0
					: frame % 4 == 2
					? 1
					: (sample_y[i] + .5) / 200;
				old = frame < history_start + history
					? 0
					: expected[frame - history][i][j];
				value = special ? 0
					: !enabled
					? sky[j]
					: floor(sky[j] * alpha +
						old * (1 - alpha) + .5);

				if (hud_on && i == 4)
					value = j == 1 ? 255 : 0;

				expected[frame][i][j] = value;
			}

		sprintf(path, "%s-direct3d-%u-%d-%02u.bmp", prefix, history,
			(int)mode, frame);

		if (success && (mode != HISTORY_QUEUE || frame == 11))
			success = mh_renderer_capture(renderer, path) &&
				capture_matches(path,
					(const double (*)[3])expected[frame]);
	}

	mh_renderer_destroy(renderer);

	return success;
}

int
main(int argc, char *argv[])
{
	unsigned int mode;
	int success;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	success = 1;

	for (mode = 0; mode < 4 && success; mode++)
		success = check_matrix_history(argv[1], mode);

	if (success)
		success = check_lens_history(argv[1], HISTORY_CAPTURE, 2) &&
			check_lens_history(argv[1], HISTORY_QUEUE, 2) &&
			check_lens_history(argv[1], HISTORY_CAPTURE, 3) &&
			check_lens_history(argv[1], HISTORY_QUEUE, 3) &&
			check_lens_history(argv[1], HISTORY_CHANGE, 3);

	if (!success)
		fprintf(stderr, "Motion blur render qualification failed: %s\n",
			SDL_GetError());

	SDL_Quit();

	if (success)
		puts("108 matrix/lens motion blur frames: N-2/N-3 history, history changes, black startup, opaque/far geometry, HUD, TRON, toggles, queued frames, diffuse alpha/gradient and texture alpha passed");

	return !success;
}
