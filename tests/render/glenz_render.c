#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct sampling_case {
	float	reflection[2], phong[2];
	double	rgb[3];
};

static int
capture_matches(const char *path, unsigned int x, const double expected[3])
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

	pixel = (const unsigned char *)rgba->pixels + 120 * rgba->pitch + x * 4;
	success = 1;

	for (channel = 0; channel < 3; channel++) {
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr,
				"pixel %u channel %u: %u expected %.6f\n", x,
				channel, pixel[channel], expected[channel]);
			success = 0;
		}
	}

	SDL_DestroySurface(rgba);

	return success;
}

static int
check_sampling_frames(struct mh_renderer *renderer, const char *path,
	struct mh_renderer_glenz *material,
	struct mh_glenz_coordinates coordinates[6])
{
	static const struct sampling_case samples[] = {
		{{.25f, .25f}, {.25f, .25f}, {70, 70, 70}},
		{{1.25f, .25f}, {.25f, .25f}, {70, 70, 70}},
		{{-.25f, .25f}, {.25f, .25f}, {50, 60, 90}},
		{{.25f, 1.75f}, {.25f, .25f}, {110, 40, 50}},
		{{1.75f, -.25f}, {.25f, .25f}, {90, 100, 30}},
		{{0, .25f}, {.25f, .25f}, {60, 65, 80}},
		{{1, .25f}, {.25f, .25f}, {60, 65, 80}},
		{{.25f, 0}, {.25f, .25f}, {90, 55, 60}},
		{{0, 0}, {.25f, .25f}, {80, 67.5, 60}},
		{{.25f, .25f}, {0, .25f}, {80, 75, 75}},
		{{.25f, .25f}, {1, .25f}, {80, 75, 75}},
		{{.25f, .25f}, {.25f, 0}, {60, 80, 80}},
		{{.25f, .25f}, {.25f, 1}, {60, 80, 80}}};

	unsigned int test, i;
	int success;

	success = mh_renderer_set_fog(renderer, NULL);

	/*
	 * Constant UVs isolate sampler addressing and bilinear edge blending.
	 */
	for (test = 0; test < sizeof(samples) / sizeof(samples[0]) && success;
		test++) {
		for (i = 0; i < 6; i++) {
			memcpy(coordinates[i].reflection,
				samples[test].reflection,
				sizeof(coordinates[i].reflection));
			memcpy(coordinates[i].phong, samples[test].phong,
				sizeof(coordinates[i].phong));
		}

		success = mh_renderer_mesh_glenz(renderer, 0, material, NULL) ==
				MH_ERROR_NONE &&
			mh_renderer_scene_frame(
				renderer, identity, identity, NULL, 1) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 220, samples[test].rgb);

		if (!success)
			fprintf(stderr, "Glenz sampling frame %u failed: %s\n",
				test, SDL_GetError());
	}

	for (i = 0; i < 6; i++) {
		coordinates[i].reflection[0] = .1f;
		coordinates[i].reflection[1] = .25f;
		coordinates[i].phong[0] = coordinates[i].phong[1] = .25f;
	}

	for (test = 1; test <= 3 && success; test++) {
		static const double linear[3] = {64, 67, 76};
		static const double nearest[3] = {70, 70, 70};

		success = mh_renderer_set_texture_filter(renderer,
				(enum mh_texture_filter)test,
				NULL) == MH_ERROR_NONE &&
			mh_renderer_mesh_glenz(renderer, 0, material, NULL) ==
				MH_ERROR_NONE &&
			mh_renderer_scene_frame(
				renderer, identity, identity, NULL, 1) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(
				path, 220, test == 1 ? nearest : linear);
	}

	if (success)
		success = mh_renderer_set_texture_filter(renderer,
				MH_TEXTURE_FILTER_ORIGINAL,
				NULL) == MH_ERROR_NONE;

	return success;
}

static int
check_distant_frames(struct mh_renderer *renderer, const char *path)
{
	static const double cue[3] = {20, 30, 50};
	static const double reflection[3] = {40, 50, 60};
	static const double phong[3] = {30, 20, 10};
	static const float depths[6] = {.7f, .75f, .9f, .8f, .9f, .45f};
	struct mh_fog fog;
	float view[16], transforms[3][16];
	double expected[3], visibility;
	unsigned int i, test;
	int success;

	/* A power-of-two far distance preserves the binary32 cutoff exactly. */
	success = mh_fog_configure(cue, 128, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_mesh_visible(renderer, 1, 0) &&
		mh_renderer_mesh_visible(renderer, 2, 0);
	memcpy(view, identity, sizeof(view));
	view[10] = -128;

	for (i = 0; i < 3; i++)
		memcpy(transforms[i], identity, sizeof(identity));

	for (test = 0; test < 6 && success; test++) {
		memcpy(transforms[0], identity, sizeof(identity));
		transforms[0][10] = 0;
		transforms[0][14] = depths[test];

		/* Only the upper-left corner keeps this source quad nearby. */
		if (test == 3) {
			transforms[0][2] = .09375f;
			transforms[0][6] = -.09375f;
		}

		visibility = test == 4 ? 1
			: test == 5    ? mh_fog_visibility(depths[test]) / 255.0
				: 0;

		for (i = 0; i < 3; i++) {
			expected[i] = cue[i] * (1 - visibility) +
				reflection[i] * visibility;

			if (test != 1 && test != 2)
				expected[i] += phong[i];
		}

		success = mh_renderer_set_fog(
				renderer, test == 4 ? NULL : &fog) &&
			mh_renderer_scene_frame(
				renderer, identity, view, transforms[0], 1) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(path, 220, expected);

		if (!success)
			fprintf(stderr, "Glenz distant frame %u failed: %s\n",
				test, SDL_GetError());
	}

	return success;
}

int
main(int argc, char *argv[])
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	static const double cue[3] = {20, 30, 50};
	static const double cover[3] = {0, 255, 0};
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char environment[4] = {20, 30, 40, 255};
	unsigned char reflection[16] = {80, 100, 120, 255, 40, 80, 160, 255,
		160, 40, 80, 255, 120, 160, 40, 255};

	unsigned char phong[16] = {60, 40, 20, 255, 100, 60, 40, 255, 20, 80,
		60, 255, 40, 20, 100, 255};

	struct mh_image images[4];
	struct mh_vertex vertices[3][6];
	struct mh_mesh meshes[3];
	struct mh_mesh_primitive quad;
	struct mh_draw draws[3];
	unsigned char groups[3] = {0, 0, 0};
	const struct mh_mesh *inputs[3];
	struct mh_glenz_coordinates coordinates[6];
	struct mh_renderer_glenz material;
	struct mh_renderer *renderer;
	struct mh_fog fog;
	float view[16], transforms[3][16];
	double expected[3], amount;
	unsigned int m, i, channel, test;
	int success, enabled, fogged, reflections;

	if (argc != 2)
		return 2;

	memset(vertices, 0, sizeof(vertices));
	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	memset(coordinates, 0, sizeof(coordinates));
	memset(images, 0, sizeof(images));
	memset(&quad, 0, sizeof(quad));
	quad.count = 6;
	images[0].rgba = white;
	images[1].rgba = environment;
	images[2].rgba = reflection;
	images[3].rgba = phong;

	for (i = 0; i < 4; i++) {
		images[i].height = i < 2 ? 1 : 2;
		images[i].width = i < 2 ? 1 : 2;
	}

	for (m = 0; m < 3; m++) {
		meshes[m].vertices = vertices[m];
		meshes[m].vertex_count = 6;
		meshes[m].draws = &draws[m];
		meshes[m].draw_count = 1;
		meshes[m].draw_groups = &groups[m];
		meshes[m].textures = images;
		meshes[m].texture_count = 4;
		meshes[m].environment_texture = 1;
		inputs[m] = &meshes[m];
		draws[m].count = 6;
		draws[m].prelit = 1;
		draws[m].environment = m != 2;

		for (i = 0; i < 6; i++) {
			vertices[m][i].position[0] =
				m == 2 ? xy[i][0] * .5f - .4f : xy[i][0];
			vertices[m][i].position[1] = xy[i][1];
			vertices[m][i].position[2] = m == 0 ? .45f
				: m == 1		    ? .6f
							: .1f;

			for (channel = 0; channel < 3; channel++)
				vertices[m][i].color[channel] = m == 2
					? (float)(cover[channel] / 255)
					: .5f;
		}
	}

	meshes[0].primitives = &quad;
	meshes[0].primitive_count = 1;
	memcpy(view, identity, sizeof(view));
	view[10] = -180;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	renderer =
		mh_sdl_renderer_create_scene_sized(NULL, inputs, 3, 320, 240);
	success = renderer != NULL &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE;
	material.coordinates = coordinates;
	material.count = 6;
	material.reflection_texture = 2;
	material.phong_texture = 3;

	/*
	 * Separate UVs; toggles; nearer ordinary occluder; farther Glenz car;
	 * rejected updates retain the previously submitted material.
	 */
	for (test = 0; test < 8 && success; test++) {
		enabled = test != 4;
		fogged = test != 3;
		reflections = test != 5;

		for (i = 0; i < 6; i++) {
			coordinates[i].reflection[0] = test == 1 ? .75f : .25f;
			coordinates[i].reflection[1] = .25f;
			coordinates[i].phong[0] = test == 2 ? .75f : .25f;
			coordinates[i].phong[1] = .25f;
		}

		success = mh_renderer_set_fog(renderer, fogged ? &fog : NULL);

		for (m = 0; m < 2 && success; m++)
			success = mh_renderer_mesh_glenz(renderer, m,
					enabled ? &material : NULL,
					NULL) == MH_ERROR_NONE;

		if (success && test == 6) {
			material.phong_texture = 4;
			success = mh_renderer_mesh_glenz(renderer, 0, &material,
					NULL) == MH_ERROR_INVALID;
			material.phong_texture = 3;
		}

		if (success && test == 7) {
			coordinates[0].phong[0] = -1;
			success = mh_renderer_mesh_glenz(renderer, 0, &material,
					NULL) == MH_ERROR_INVALID;
		}

		amount = fogged ? (255 - mh_fog_visibility(.45f)) / 255.0 : 0;

		for (channel = 0; channel < 3; channel++) {
			expected[channel] = !reflections ? 255
				: !enabled
				? environment[channel]
				: reflection[(test == 1 ? 4 : 0) + channel];
			expected[channel] =
				expected[channel] * .5 * (1 - amount) +
				cue[channel] * amount;

			if (enabled && reflections)
				expected[channel] +=
					phong[(test == 2 ? 4 : 0) + channel] *
					.5;
		}

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, NULL, reflections) &&
				mh_renderer_capture(renderer, argv[1]) &&
				capture_matches(argv[1], 220, expected) &&
				capture_matches(argv[1], 100, cover);

		if (!success)
			fprintf(stderr, "Glenz frame %u failed: %s\n", test,
				SDL_GetError());
	}

	/*
	 * Clip only the upper-left source corner. The sampled lower-right
	 * triangle is wholly inside: both halves must still lose Phong.
	 * Exact near equality clips too; crossing a side plane does not.
	 */
	for (m = 0; m < 3; m++)
		memcpy(transforms[m], identity, sizeof(identity));

	for (i = 0; i < 6; i++) {
		coordinates[i].reflection[0] = .25f;
		coordinates[i].phong[0] = .25f;
	}

	if (success)
		success = mh_renderer_set_fog(renderer, NULL) &&
			mh_renderer_mesh_glenz(renderer, 0, &material, NULL) ==
				MH_ERROR_NONE;

	for (test = 0; test < 5 && success; test++) {
		memcpy(transforms[0], identity, sizeof(identity));

		if (test < 3) {
			transforms[0][2] = .25f;
			transforms[0][6] = -.25f;
			transforms[0][10] = 0;
			transforms[0][14] = test == 0 ? .45f
				: test == 1	      ? .35f
						: .4f;
		} else if (test == 3)
			transforms[0][12] = .5f;

		for (channel = 0; channel < 3; channel++) {
			expected[channel] = reflection[channel] * .5;

			if (test != 1 && test != 2)
				expected[channel] += phong[channel] * .5;
		}

		success = mh_renderer_scene_frame(
				renderer, identity, view, transforms[0], 1) &&
			mh_renderer_capture(renderer, argv[1]) &&
			capture_matches(argv[1], 220, expected);

		if (!success)
			fprintf(stderr, "Glenz clipping frame %u failed: %s\n",
				test, SDL_GetError());
	}

	if (success)
		success = check_distant_frames(renderer, argv[1]);

	if (success)
		success = check_sampling_frames(
			renderer, argv[1], &material, coordinates);

	if (!success)
		fprintf(stderr, "Glenz renderer: %s\n", SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("Glenz: 35 GPU frames passed (maps, fog, depth, toggles, rejected updates, clipping, distant faces, repeat sampling, filtering)");

	return success ? 0 : 1;
}
