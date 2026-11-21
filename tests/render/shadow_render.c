#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

enum shadow_case {
	SINGLE_SHADOW,
	OVERLAPPING_SHADOWS,
	OCCLUDED_SHADOW,
	ADDITIVE_BEHIND_SHADOW,
	VARYING_DEPTH_SHADOW,
	BIASED_SHADOW,
	DEEP_SHADOW,
	UNBIASED_SHADOW,
	SHADOW_CASES
};

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
make_quad(struct mh_vertex v[6], float depth, float left, float right)
{
	static const float xy[6][2] = {
		{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

	unsigned int i, j;

	memset(v, 0, 6 * sizeof(*v));

	for (i = 0; i < 6; i++) {
		memcpy(v[i].position, xy[i], sizeof(xy[i]));
		v[i].position[2] = depth;

		for (j = 0; j < 3; j++)
			v[i].color[j] = xy[i][0] < 0 ? left : right;
	}
}

/* Map the same screen rectangles into camera space for explicit lens frames. */
static void
project_fixture(struct mh_vertex vertices[6], float raw[18], unsigned int mesh,
	unsigned int scenario)
{
	struct mh_vertex *vertex;
	float depth;
	unsigned int i, j;

	for (i = 0; i < 6; i++) {
		vertex = &vertices[i];
		depth = vertex->position[2];

		if (scenario == VARYING_DEPTH_SHADOW && mesh < 2) {
			vertex->position[0] /= depth;
			vertex->position[1] /= depth;
		}

		depth *= 180;

		/* Slightly behind road: bias, not world lift. */
		if (mesh == 1 && scenario >= BIASED_SHADOW)
			depth = scenario == DEEP_SHADOW ? 163 : 162.02f;

		vertex->position[0] *= depth;
		vertex->position[1] *= depth * 2 / 3;
		vertex->position[2] = -depth;

		for (j = 0; j < 3; j++)
			raw[i * 3 + j] = vertex->color[j] * 255;
	}
}

static int
draw_frame(struct mh_renderer *renderer, const struct mh_projection *lens,
	const float matrix[16], const float view[16])
{
	if (lens != NULL)
		return mh_renderer_scene_frame_lens(renderer, lens, identity,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE;

	return mh_renderer_scene_frame(renderer, matrix, view, NULL, 0);
}

int
main(int argc, char *argv[])
{
	struct mh_vertex vertices[4][6];
	struct mh_mesh mesh[4];
	struct mh_draw draws[4];
	struct mh_image textures[4];
	const struct mh_mesh *meshes[4];
	unsigned char pixels[4][4] = {{180, 120, 60, 255}, {255, 255, 255, 0},
		{255, 255, 255, 255}, {40, 20, 10, 255}};

	struct mh_renderer *renderer;
	struct mh_fog fog;
	struct mh_depth_probe probe;
	struct mh_projection lens;
	const struct mh_projection *active_lens;
	unsigned char visible;
	const double cue[3] = {180, 120, 60};
	double expected[3], transmission, t;
	float view[16], projection[16];
	char path[1024];
	unsigned int test, scenario, i, j, m;
	float raw[4][18];
	int success;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	memset(mesh, 0, sizeof(mesh));
	memset(draws, 0, sizeof(draws));

	for (i = 0; i < 4; i++) {
		textures[i].width = textures[i].height = 1;
		textures[i].rgba = pixels[i];
		mesh[i].vertices = vertices[i];
		mesh[i].vertex_count = 6;
		mesh[i].draws = &draws[i];
		mesh[i].draw_count = 1;
		draws[i].count = 6;
		draws[i].prelit = 1;
		mesh[i].textures = &textures[i];
		mesh[i].texture_count = 1;
		meshes[i] = &mesh[i];
	}

	memcpy(view, identity, sizeof(view));
	view[10] = -180;
	memcpy(projection, identity, sizeof(projection));
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	success = mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE;

	for (test = 0; test < BIASED_SHADOW + SHADOW_CASES && success; test++) {
		scenario = test < BIASED_SHADOW ? test : test - BIASED_SHADOW;
		active_lens = test < BIASED_SHADOW ? NULL : &lens;
		memcpy(projection, identity, sizeof(projection));
		make_quad(vertices[0], .9f, 1, 1);
		make_quad(vertices[1], .2f, 100.0f / 255, 220.0f / 255);
		make_quad(vertices[2], .4f, .5f, .5f);
		make_quad(vertices[3], scenario == OCCLUDED_SHADOW ? .1f : .6f,
			1, 1);

		if (scenario == VARYING_DEPTH_SHADOW) {
			/*
			 * Same screen rectangle, unequal W: multiplier stays
			 * screen-linear.
			 */
			projection[10] = .5f;
			projection[11] = 1;
			projection[15] = 0;

			for (i = 0; i < 6; i++) {
				vertices[1][i].position[2] =
					vertices[1][i].position[0] < 0 ? .2f
								: .7f;

				for (j = 0; j < 2; j++)
					vertices[1][i].position[j] *=
						vertices[1][i].position[2];
			}

			for (i = 0; i < 6; i++)
				for (j = 0; j < 2; j++)
					vertices[0][i].position[j] *= .9f;
		}

		if (active_lens != NULL) {
			success =
				mh_projection_setup(960, 640, 4.0f / 3, 1, .5f,
					180, &lens, NULL) == MH_ERROR_NONE;

			for (m = 0; m < 4; m++) {
				project_fixture(
					vertices[m], raw[m], m, scenario);
				mesh[m].material_rgb = raw[m];
				mesh[m].material_rgb_count = 6;
			}
		}

		if (scenario == UNBIASED_SHADOW) {
			/*
			 * Same recessed geometry, matrix entry keeps bias
			 * disabled.
			 */
			active_lens = NULL;
			memcpy(view, identity, sizeof(view));
			projection[5] = 1.5f;
			projection[10] = projection[11] = -1;
			projection[14] = -.5f;
			projection[15] = 0;
		}

		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 4);
		success = success && renderer != NULL &&
			mh_renderer_set_fog(renderer, &fog) &&
			mh_renderer_mesh_shadows(renderer, 1) &&
			mh_renderer_mesh_shadows(renderer, 2) &&
			mh_renderer_mesh_visible(
				renderer, 2, scenario == OVERLAPPING_SHADOWS) &&
			mh_renderer_mesh_visible(renderer, 3,
				scenario == OCCLUDED_SHADOW ||
					scenario == ADDITIVE_BEHIND_SHADOW);

		/*
		 * Later additive effect behind the shadow proves pass order AND
		 * no depth write. Foreground ordinary geometry must occlude it
		 * instead.
		 */
		if (scenario == ADDITIVE_BEHIND_SHADOW && success)
			success = mh_renderer_mesh_unavailable(
				renderer, 3, vertices[3]);

		sprintf(path, "%s-case-%u.bmp", argv[1], test);

		if (success)
			success = draw_frame(renderer, active_lens, projection,
					view) &&
				mh_renderer_capture(renderer, path);

		t = 240.5 / 960;
		transmission = active_lens != NULL
			? 100.0 / 255
			: (100 * (1 - t) + 220 * t) / 255;

		for (j = 0; j < 3; j++) {
			expected[j] = scenario == OCCLUDED_SHADOW
				? pixels[3][j]
				: cue[j] * transmission;

			if (scenario == OVERLAPPING_SHADOWS)
				expected[j] *= .5;

			if (scenario >= DEEP_SHADOW)
				expected[j] = cue[j];

			if (scenario == ADDITIVE_BEHIND_SHADOW)
				expected[j] += pixels[3][j];
		}

		if (success)
			success = capture_matches(path, 240, 320, expected);

		memset(&probe, 0, sizeof(probe));
		probe.active = 1;
		probe.x = 240;
		probe.y = 320;
		probe.depth = .1f;
		visible = 0;

		if (success)
			success =
				mh_renderer_depth_queue(renderer, &probe, 1) &&
				mh_renderer_depth_read(renderer, &visible, 1) &&
				draw_frame(renderer, active_lens, projection,
					view) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 240, 320, expected);

		mh_renderer_destroy(renderer);
	}

	if (!success)
		fprintf(stderr, "Shadow pixel qualification failed: %s\n",
			SDL_GetError());

	SDL_Quit();

	if (success)
		puts("Matrix/lens shadow multiplier, interpolated/flat shade, fog bypass, occlusion, pass order, no-depth-write and lens bias pixels passed");

	return !success;
}
