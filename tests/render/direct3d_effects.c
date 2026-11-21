#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum effect_mesh {
	ROAD,
	HALO,
	SHADOW,
	PARTICLE,
	UNAVAILABLE,
	SKY,
	MESH_COUNT
};

enum effect_case {
	PASS_ORDER,
	FLAT_HALO,
	FLAT_PARTICLE,
	FLAT_UNAVAILABLE,
	PARTICLE_ALPHA,
	BIASED_HALO,
	BIASED_PARTICLE,
	BIASED_UNAVAILABLE,
	OCCLUDED_HALO,
	OCCLUDED_PARTICLE,
	OCCLUDED_UNAVAILABLE,
	SKY_DEPTH,
	SKY_FOREGROUND,
	SKY_BLUR_DEPTH,
	EFFECT_CASES
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void
make_quad(struct mh_vertex vertices[6], float raw[18], float depth, float left,
	float right, int camera_space)
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	float distance, color;
	unsigned int i, channel;

	memset(vertices, 0, 6 * sizeof(*vertices));
	distance = .5f / (1 - depth);

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = depth;
		vertices[i].opacity = 127.0f / 255;

		if (camera_space) {
			vertices[i].position[0] *= distance;
			vertices[i].position[1] *= distance * .75f;
			vertices[i].position[2] = -distance;
		}

		color = xy[i][0] < 0 ? left : right;

		for (channel = 0; channel < 3; channel++) {
			raw[i * 3 + channel] = color;
			vertices[i].color[channel] = color / 255;
		}
	}
}

static int
capture_matches(const char *path, double expected)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int channel;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	pixel = (const unsigned char *)rgba->pixels + 120 * rgba->pitch +
		200 * 4;
	ok = 1;

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected) > 1.1) {
			fprintf(stderr, "channel %u: %u expected %.6f\n",
				channel, pixel[channel], expected);
			ok = 0;
		}

	SDL_DestroySurface(rgba);

	return ok;
}

int
main(int argc, char *argv[])
{
	static const float colors[MESH_COUNT] = {64, 32, 128, 16, 8, 32};
	struct mh_mesh meshes[MESH_COUNT];
	const struct mh_mesh *list[MESH_COUNT];
	struct mh_vertex vertices[MESH_COUNT][6];
	struct mh_draw draws[MESH_COUNT];
	struct mh_image textures[2];
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_renderer *gpu;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char transparent[4] = {255, 255, 255, 0};
	const double cue[3] = {64, 64, 64};
	float raw[MESH_COUNT][18], depth, right;
	double expected;
	unsigned int m, test, direct3d, selected;
	int ok, visible;

	if (argc != 2)
		return 2;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	ok = 1;

	for (direct3d = 0; direct3d < 2 && ok; direct3d++) {
		memset(meshes, 0, sizeof(meshes));
		memset(draws, 0, sizeof(draws));
		textures[0].width = textures[0].height = 1;
		textures[0].rgba = white;
		textures[1] = textures[0];
		textures[1].rgba = transparent;

		for (m = 0; m < MESH_COUNT; m++) {
			make_quad(vertices[m], raw[m], .5f, colors[m],
				colors[m], 0);
			meshes[m].vertices = vertices[m];
			meshes[m].vertex_count = 6;
			meshes[m].material_rgb = raw[m];
			meshes[m].material_rgb_count = 6;
			meshes[m].draws = &draws[m];
			meshes[m].draw_count = 1;
			meshes[m].textures = textures;
			meshes[m].texture_count = 2;
			draws[m].count = 6;
			draws[m].prelit = 1;
			list[m] = &meshes[m];
		}

		gpu = mh_sdl_renderer_create_scene_sized(
			NULL, list, MESH_COUNT, 320, 240);
		ok = gpu != NULL &&
			mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
				&projection, NULL) == MH_ERROR_NONE &&
			mh_fog_configure(cue, 180, &fog, NULL) ==
				MH_ERROR_NONE &&
			mh_renderer_set_fog(gpu, &fog);

		for (test = 0; test < EFFECT_CASES && ok; test++) {
			selected = test == FLAT_HALO || test == BIASED_HALO ||
					test == OCCLUDED_HALO
				? HALO
				: test == FLAT_UNAVAILABLE ||
					test == BIASED_UNAVAILABLE ||
					test == OCCLUDED_UNAVAILABLE
				? UNAVAILABLE
				: PARTICLE;
			expected = 64;

			if (test == PASS_ORDER)
				expected = direct3d
					? (64 + 32) * 128.0 / 255 + 16 + 8
					: 64 * 128.0 / 255 + 16 + 8 + 32;
			else if (test <= FLAT_UNAVAILABLE)
				expected += direct3d
					? colors[selected]
					: colors[selected] + 42.125;
			else if (test < OCCLUDED_HALO && direct3d)
				expected += colors[selected];

			if (test >= SKY_DEPTH)
				expected = direct3d && test != SKY_FOREGROUND
					? 32
					: 64;

			/*
			 * The second blur buffer retains the preceding
			 * foreground.
			 */
			if (test == SKY_BLUR_DEPTH && direct3d)
				expected = 32 * 127.0 / 255 + 64 * 128.0 / 255;

			for (m = 0; m < MESH_COUNT; m++) {
				visible = m == ROAD || test == PASS_ORDER ||
					m == selected;

				if (test >= SKY_DEPTH)
					visible = m == ROAD || m == SKY;
				else if (m == SKY)
					visible = 0;

				depth = m == ROAD ? .95f : .2f + m * .1f;
				right = colors[m];

				if (m == selected && test >= BIASED_HALO)
					depth = test < OCCLUDED_HALO ? .9500005f
								: .95001f;

				if (m == selected && test >= FLAT_HALO &&
					test <= FLAT_UNAVAILABLE)
					right += 64;

				if (test >= SKY_DEPTH)
					depth = m == SKY		 ? .6f
						: test == SKY_FOREGROUND ? .4f
									: .8f;

				make_quad(vertices[m], raw[m], depth, colors[m],
					right,
					direct3d &&
						(m == ROAD || m == SHADOW ||
							m == UNAVAILABLE));
				draws[m].texture =
					m == PARTICLE && test == PARTICLE_ALPHA
					? 1
					: 0;
				ok = ok &&
					mh_renderer_mesh_visible(
						gpu, m, visible);
			}

			ok = ok &&
				mh_renderer_mesh_lit(
					gpu, ROAD, vertices[ROAD]) &&
				mh_renderer_mesh_halos(gpu, HALO) &&
				mh_renderer_mesh_shadows(gpu, SHADOW) &&
				mh_renderer_mesh_particles(gpu, PARTICLE) &&
				mh_renderer_mesh_sky(gpu, SKY) &&
				mh_renderer_set_motion_blur(
					gpu, test == SKY_BLUR_DEPTH) &&
				mh_renderer_mesh_lit(gpu, UNAVAILABLE,
					vertices[UNAVAILABLE]) &&
				mh_renderer_mesh_unavailable(gpu, UNAVAILABLE,
					vertices[UNAVAILABLE]);

			if (ok)
				ok = direct3d
					? mh_renderer_scene_frame_lens(gpu,
						&projection, identity, NULL,
						1, NULL, NULL, NULL,
						NULL) == MH_ERROR_NONE
					: mh_renderer_scene_frame(gpu, identity,
						identity, NULL, 1);

			/*
			 * Exercise the history-blended sky pipeline on a second
			 * frame.
			 */
			if (ok && test == SKY_BLUR_DEPTH)
				ok = direct3d
					? mh_renderer_scene_frame_lens(gpu,
						&projection, identity, NULL,
						1, NULL, NULL, NULL,
						NULL) == MH_ERROR_NONE
					: mh_renderer_scene_frame(gpu, identity,
						identity, NULL, 1);

			ok = ok && mh_renderer_capture(gpu, argv[1]) &&
				capture_matches(argv[1], expected);

			if (!ok)
				fprintf(stderr,
					"Effect case %u Direct3D=%u: %s\n",
					test, direct3d, SDL_GetError());
		}

		mh_renderer_destroy(gpu);
	}

	SDL_Quit();

	if (ok)
		puts("28 Glide/Direct3D effect cases: order, flat shading, alpha bypass, inherited bias, occlusion, late-effect no-depth-write and sky/blur depth policy pass");

	return !ok;
}
