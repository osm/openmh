#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/image/provider.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"
#include "engine/render/atmosphere_sky.h"

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

static void
make_quad(struct mh_vertex vertices[6], float left, float right, float depth,
	float u)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i, corner, channel;

	memset(vertices, 0, 6 * sizeof(*vertices));

	for (i = 0; i < 6; i++) {
		corner = corners[i];
		vertices[i].position[0] =
			corner == 0 || corner == 3 ? left : right;
		vertices[i].position[1] = corner < 2 ? 1 : -1;
		vertices[i].position[2] = depth;
		vertices[i].uv[0] = corner == 0 || corner == 3 ? 0 : u;
		vertices[i].uv[1] = .5f;

		for (channel = 0; channel < 3; channel++)
			vertices[i].color[channel] = 1;
	}
}

static int
check_sky_fill(const char *path)
{
	static const unsigned char above[3] = {71, 13, 14};
	static const unsigned char below[3] = {129, 38, 15};
	static const double expected_top[3] = {64, 12, 8};
	static const double expected_bottom[3] = {128, 36, 8};
	struct mh_mesh mesh;
	const struct mh_mesh *input;
	struct mh_track track;
	struct mh_sky_state state;
	struct mh_projection projection;
	struct mh_renderer *renderer;
	unsigned char tga[22];
	unsigned int image, lens;
	int success;

	memset(tga, 0, sizeof(tga));
	tga[2] = 3;
	tga[12] = tga[14] = 2;
	tga[16] = 8;
	tga[17] = 32;

	if (mh_sky_mesh_create(mh_image_fallback_decoder(), tga, sizeof(tga),
		&mesh, NULL) != MH_ERROR_NONE)
		return 0;

	memset(&track, 0, sizeof(track));
	track.horizon = .5f;
	memcpy(track.sky_top, above, sizeof(above));
	memcpy(track.sky_bottom, below, sizeof(below));
	input = &mesh;
	renderer =
		mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);
	success = renderer != NULL &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
			&projection, NULL) == MH_ERROR_NONE;

	for (image = 0; image < 2 && success; image++) {
		memset(&state, 0, sizeof(state));
		success = (image ? mh_sky_mesh_update(&mesh, &state, &track,
					identity, identity, 320, 240, NULL)
				: mh_sky_mesh_plain(&mesh, &state, &track,
					identity, identity, 320, 240,
					NULL)) == MH_ERROR_NONE &&
			mh_renderer_mesh_sky(renderer, 0);

		for (lens = 0; lens < 2 && success; lens++) {
			success = lens
				? mh_renderer_scene_frame_lens(renderer,
					&projection, identity, NULL, 1, NULL,
					NULL, NULL, NULL) == MH_ERROR_NONE
				: mh_renderer_scene_frame(renderer, identity,
					identity, NULL, 1);
			success = success &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 160, 60, expected_top) &&
				capture_matches(
					path, 160, 180, expected_bottom);
		}
	}

	mh_renderer_destroy(renderer);
	mh_mesh_free(&mesh);

	if (success)
		puts("Four sky-fill frames pass patched RGB precision with matrix/lens submission and panorama On/Off");

	return success;
}

int
main(int argc, char *argv[])
{
	struct mh_vertex vertices[3][6];
	struct mh_mesh meshes[3];
	struct mh_draw draws[3];
	struct mh_image textures[3];
	const struct mh_mesh *inputs[3];

	/* Zero-alpha glow verifies that this pass does not apply cutout. */
	unsigned char glow[4] = {50, 25, 10, 0};
	unsigned char wall[4] = {20, 40, 60, 255};
	unsigned char sky[8] = {40, 60, 80, 255, 80, 90, 100, 255};
	struct mh_renderer *renderer;
	struct mh_fog fog;
	struct mh_depth_probe probes[7];
	unsigned char history[7];
	const double cue[3] = {200, 0, 0};
	const double background[3] = {40, 60, 80}, visible[3] = {90, 85, 90};
	const double hidden[3] = {20, 40, 60}, front[3] = {70, 65, 70};
	float view[16];
	char path[1024];
	unsigned int i;
	int success;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	textures[0].rgba = glow;
	textures[1].rgba = wall;
	textures[2].rgba = sky;

	for (i = 0; i < 3; i++) {
		textures[i].width = i == 2 ? 2 : 1;
		textures[i].height = 1;
		meshes[i].vertices = vertices[i];
		meshes[i].vertex_count = 6;
		meshes[i].draws = &draws[i];
		meshes[i].draw_count = 1;
		meshes[i].textures = &textures[i];
		meshes[i].texture_count = 1;
		draws[i].count = 6;
		draws[i].prelit = 1;
		inputs[i] = &meshes[i];
	}

	make_quad(vertices[0], -.5f, .5f, .6f, 1);
	make_quad(vertices[1], 0, 1, .1f, 1);
	make_quad(vertices[2], -1, 1, 0, 4);
	memcpy(view, identity, sizeof(view));
	view[10] = -180;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	renderer = mh_sdl_renderer_create_scene(NULL, inputs, 3);
	success = renderer != NULL &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(renderer, &fog) &&
		mh_renderer_mesh_sky(renderer, 2) &&
		mh_renderer_mesh_halos(renderer, 0);
	sprintf(path, "%s-occluded.bmp", argv[1]);

	if (success)
		success = mh_renderer_scene_frame(
				renderer, identity, view, NULL, 0) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 60, 320, background) &&
			capture_matches(path, 300, 320, visible) &&
			capture_matches(path, 540, 320, hidden) &&
			capture_matches(path, 900, 320, hidden);

	make_quad(vertices[0], -.5f, .5f, .05f, 1);
	sprintf(path, "%s-front.bmp", argv[1]);

	if (success)
		success = mh_renderer_mesh_halos(renderer, 0) &&
			mh_renderer_scene_frame(
				renderer, identity, view, NULL, 0) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 540, 320, front);

	/* Hide the wall: the second U repeat must match the first. */
	sprintf(path, "%s-repeat.bmp", argv[1]);

	if (success)
		success = mh_renderer_mesh_visible(renderer, 0, 0) &&
			mh_renderer_mesh_visible(renderer, 1, 0) &&
			mh_renderer_scene_frame(
				renderer, identity, view, NULL, 0) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 60, 320, background) &&
			capture_matches(path, 300, 320, background) &&
			capture_matches(path, 540, 320, background) &&
			capture_matches(path, 780, 320, background);

	/*
	 * Single-source readback uses integer pixel truncation and a strict
	 * depth comparison. Previous visibility changes only on consumption.
	 */
	memset(probes, 0, sizeof(probes));
	memset(history, 0, sizeof(history));

	for (i = 0; i < 7; i++) {
		probes[i].active = 1;
		probes[i].x = 540.5f;
		probes[i].y = 100.5f;
		probes[i].depth = .6f;
	}

	probes[0].x = 300.5f;
	probes[2].depth = .1f;
	probes[3].depth = .05f;
	probes[4].x = 479.9f;
	probes[5].y = 540.5f;
	probes[6].active = 0;
	history[6] = 1;
	make_quad(vertices[1], 0, 1, .1f, 1);

	for (i = 0; i < 6; i++)
		vertices[1][i].position[1] =
			vertices[1][i].position[1] * .5f + .5f;

	if (success)
		success = mh_renderer_depth_read(renderer, history, 7) &&
			history[6] == 1 &&
			mh_renderer_mesh_visible(renderer, 1, 1) &&
			mh_renderer_update_mesh(renderer, 1) &&
			mh_renderer_scene_frame(
				renderer, identity, view, NULL, 0) &&
			mh_renderer_depth_queue(renderer, probes, 7) &&
			history[0] == 0 &&
			!mh_renderer_depth_queue(renderer, probes, 7) &&
			!mh_renderer_depth_read(renderer, history, 6) &&
			mh_renderer_depth_read(renderer, history, 7) &&
			history[0] == 1 && history[1] == 0 && history[2] == 0 &&
			history[3] == 1 && history[4] == 1 && history[5] == 1 &&
			history[6] == 1;

	probes[0].x = -1;

	if (success)
		success = !mh_renderer_depth_queue(renderer, probes, 7);

	probes[0].x = 300.5f;

	if (success)
		success = mh_renderer_mesh_visible(renderer, 1, 0) &&
			mh_renderer_scene_frame(
				renderer, identity, view, NULL, 0) &&
			mh_renderer_depth_queue(renderer, probes, 7) &&
			history[1] == 0 &&
			mh_renderer_depth_read(renderer, history, 7) &&
			history[1] == 1 && history[2] == 1;

	/* Teardown must own and release an outstanding readback fence too. */
	if (success)
		success = mh_renderer_depth_queue(renderer, probes, 7);

	if (!success)
		fprintf(stderr, "Atmosphere render qualification failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	sprintf(path, "%s-fill.bmp", argv[1]);

	if (success)
		success = check_sky_fill(path);

	SDL_Quit();

	if (success)
		puts("Sky/halo pixels and source depth queries/history/equality/coordinates/lifetime passed");

	return !success;
}
