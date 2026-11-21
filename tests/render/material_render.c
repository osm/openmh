#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const float xy[6][2] = {
	{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};

struct fixture {
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[6];
	struct mh_draw			draw;
	struct mh_mesh_primitive	primitive;
	struct mh_image			image;
	unsigned char			pixels[8];
};

static void
make_quad(struct fixture *f, const unsigned char rgb[3], float depth)
{
	unsigned int i;

	memset(f, 0, sizeof(*f));
	f->mesh.vertices = f->vertices;
	f->mesh.vertex_count = 6;
	f->mesh.draws = &f->draw;
	f->mesh.draw_count = 1;
	f->mesh.primitives = &f->primitive;
	f->mesh.primitive_count = 1;
	f->primitive.count = 6;
	f->mesh.textures = &f->image;
	f->mesh.texture_count = 1;
	f->draw.count = 6;
	f->draw.prelit = 1;
	f->image.width = f->image.height = 1;
	f->image.rgba = f->pixels;
	memcpy(f->pixels, rgb, 3);
	f->pixels[3] = 255;

	for (i = 0; i < 6; i++) {
		memcpy(f->vertices[i].position, xy[i], sizeof(xy[i]));
		f->vertices[i].position[2] = depth;
		f->vertices[i].uv[0] = f->vertices[i].uv[1] = .5f;
		f->vertices[i].color[0] = f->vertices[i].color[1] =
			f->vertices[i].color[2] = 1;
	}
}

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

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr,
				"%s pixel %u,%u channel %u: %u expected %.4f\n",
				path, x, y, channel, pixel[channel],
				expected[channel]);
			success = 0;
		}

	SDL_DestroySurface(rgba);

	return success;
}

/*
 * Independently expected screen-linear ramps on a trapezoid crossing two
 * viewport boundaries. Rotations cover all four screen corners. The old
 * noperspective shader zeroed RGB on part of this clipped geometry.
 */
static int
check_clipped_attributes(const char *prefix)
{
	static const unsigned char sky[3] = {20, 40, 100},
				red[3] = {180, 20, 10};

	const double cue[3] = {130, 39, 16};
	struct fixture f[2];
	const struct mh_mesh *meshes[2];
	struct mh_renderer *renderer;
	struct mh_fog fog;
	float projection[16], view[16], px, py, w;
	double expected[3], sx, sy, original_y, t, shade, fade;
	unsigned int test, rotation, sample, i, j, x, y;
	char path[1024];
	int success;

	meshes[0] = &f[0].mesh;
	meshes[1] = &f[1].mesh;
	memcpy(projection, identity, sizeof(projection));
	projection[10] = .5f;
	projection[11] = 1;
	projection[15] = 0;
	success = mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE;

	for (test = 0; test < 5 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .5f);
		memcpy(view, identity, sizeof(view));

		/* Fog: top depth .2, bottom .7. Far fade: .8 and .94. */
		if (test == 2) {
			view[10] = 15;
			view[14] = -156;
		}

		if (test >= 3) {
			view[10] = 4.2f;
			view[14] = -177.6f;
		}

		f[1].draw.alpha_test = test == 4;
		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 2);
		success = renderer != NULL && mh_renderer_mesh_sky(renderer, 0);

		if (success && test >= 2)
			success = mh_renderer_set_fog(renderer, &fog);

		for (rotation = 0; rotation < 4 && success; rotation++) {
			for (j = 0; j < 6; j++) {
				w = xy[j][1] > 0 ? 8.0f : 2.0f;
				px = xy[j][0] * 3;
				py = xy[j][1] > 0 ? -1.4f : -2.4f;
				f[1].vertices[j].position[0] = rotation == 0
					? px
					: rotation == 1 ? -py
					: rotation == 2 ? -px
							: py;
				f[1].vertices[j].position[1] = rotation == 0
					? py
					: rotation == 1 ? px
					: rotation == 2 ? -py
							: -px;
				f[1].vertices[j].position[2] = w;

				for (i = 0; i < 3; i++)
					f[1].vertices[j].color[i] = test == 1
						? (xy[j][1] > 0 ? .25f : .75f)
						: .5f;
			}

			sprintf(path, "%s-clipped-%u-%u.bmp", prefix, test,
				rotation);
			success = mh_renderer_update_mesh(renderer, 1) &&
				mh_renderer_scene_frame(
					renderer, projection, view, NULL, 0) &&
				mh_renderer_capture(renderer, path);

			for (sample = 0; sample < 2 && success; sample++) {
				sx = sample ? .2 : -.25;
				sy = sample ? -.4 : -.85;
				x = (unsigned int)(((rotation == 0 ? sx
								: rotation ==
									1
								? -sy
								: rotation ==
									2
								? -sx
								: sy) +
							1) *
					480);
				y = (unsigned int)((1 -
							(rotation == 0 ? sy
									: rotation ==
										1
									? sx
									: rotation ==
										2
									? -sy
									: -sx)) *
					320);
				sx = (x + .5) / 480 - 1;
				sy = 1 - (y + .5) / 320;
				original_y = rotation == 0 ? sy
					: rotation == 1	   ? -sx
					: rotation == 2	   ? -sy
							: sx;
				t = (-.175 - original_y) / 1.025;
				shade = test == 1 ? .25 + .5 * t : .5;
				fade = (mh_fog_background(.8f) * (1 - t) +
					mh_fog_background(.94f) * t) /
					255;

				for (i = 0; i < 3; i++)
					expected[i] = test >= 3
						? cue[i] * (1 - fade) +
							sky[i] * fade
						: test == 2
						? red[i] * shade * (1 - t) +
							cue[i] * t
						: red[i] * shade;

				success = capture_matches(path, x, y, expected);
			}
		}

		mh_renderer_destroy(renderer);
	}

	return success;
}

int
main(int argc, char *argv[])
{
	static const unsigned char sky[3] = {20, 40, 100},
				red[3] = {180, 20, 10},

				green[3] = {0, 100, 0};

	static const float depths[] = {
		.700001f, .75f, .8f, .85f, .9f, .95f, .99f};

	const double cue[3] = {130, 39, 16};
	struct fixture f[3];
	const struct mh_mesh *meshes[3];
	struct mh_fog fog;
	struct mh_renderer *renderer;
	float view[16], projection[16], models[48];
	double expected[3], amount, t, left, right;
	char path[1024];
	unsigned int i, j, test;
	int success;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}

	memcpy(view, identity, sizeof(view));
	view[10] = -180;

	for (i = 0; i < 3; i++) {
		meshes[i] = &f[i].mesh;
		memcpy(models + i * 16, identity, sizeof(identity));
	}

	success = mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE;

	/*
	 * Exact-zero rejection and POINT sampling, including alpha 1/255 which
	 * the old .5 cutoff rejected. Near cutouts do not use alpha blending.
	 */
	for (test = 0; test < 4 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .1f);
		make_quad(&f[2], green, .4f);
		f[1].draw.alpha_test = 1;

		if (test < 2)
			f[1].pixels[3] = (unsigned char)test;
		else {
			f[1].image.width = 2;
			f[1].pixels[3] = 0;
			memcpy(f[1].pixels + 4, green, 3);
			f[1].pixels[7] = 255;

			for (i = 0; i < 6; i++)
				f[1].vertices[i].uv[0] =
					test == 2 ? .49f : .51f;
		}

		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_mesh_visible(renderer, 2, 0);

		for (j = 0; j < 3; j++)
			expected[j] = test == 0 || test == 2 ? sky[j]
				: test == 1		     ? red[j]
							: green[j];

		sprintf(path, "%s-cutout-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);

		mh_renderer_destroy(renderer);
	}

	/*
	 * The material flag and image format are independent. P8 remains opaque
	 * even in an alpha-test draw; 1555 quantizes RGB and its one-bit alpha.
	 */
	for (test = 0; test < 4 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .1f);
		make_quad(&f[2], green, .4f);
		f[1].draw.alpha_test = 1;
		f[1].pixels[3] = test == 0 ? 0
			: test == 1	   ? 127
			: test == 2	   ? 128
					: 255;
		success = mh_image_texture_format(&f[1].image,
				test == 0 ? MH_TEXTURE_PALETTE_RGB
					: MH_TEXTURE_ARGB1555,
				NULL) == MH_ERROR_NONE;

		if (!success)
			break;

		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_mesh_visible(renderer, 2, 0);

		for (j = 0; j < 3; j++)
			expected[j] = test == 0 ? red[j]
				: test == 1	? sky[j]
				: j == 0	? 181
				: j == 1	? 16
						: 8;

		sprintf(path, "%s-format-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);

		mh_renderer_destroy(renderer);
	}

	/* Fully distant solids and alpha faces fade fog RGB into the sky. */
	for (test = 0; test < 2 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .9f);
		make_quad(&f[2], green, .4f);
		f[1].draw.alpha_test = (int)test;
		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_mesh_visible(renderer, 2, 0) &&
			mh_renderer_set_fog(renderer, &fog);

		for (i = 0; i < sizeof(depths) / sizeof(depths[0]) && success;
			i++) {
			for (j = 0; j < 6; j++)
				f[1].vertices[j].position[2] = depths[i];

			amount = mh_fog_background(depths[i]) / 255.0;

			for (j = 0; j < 3; j++)
				expected[j] =
					cue[j] * (1 - amount) + sky[j] * amount;

			sprintf(path, "%s-distant-%u-%u.bmp", argv[1], test, i);
			success = mh_renderer_update_mesh(renderer, 1) &&
				mh_renderer_scene_frame(
					renderer, identity, view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);
		}

		/*
		 * Vertex fade is screen-linear, with quantization before
		 * interpolation.
		 */
		for (j = 0; j < 6; j++)
			f[1].vertices[j].position[2] =
				xy[j][0] < 0 ? .8f : .94f;

		left = mh_fog_background(.8f) / 255.0;
		right = mh_fog_background(.94f) / 255.0;
		memcpy(projection, identity, sizeof(projection));

		for (i = 0; i < 2 && success; i++) {
			if (i) {
				projection[10] = .5f;
				projection[11] = 1;
				projection[15] = 0;

				for (j = 0; j < 6; j++) {
					f[1].vertices[j].position[0] *=
						f[1].vertices[j].position[2];
					f[1].vertices[j].position[1] *=
						f[1].vertices[j].position[2];
				}
			}

			sprintf(path, "%s-interpolation-%u-%u.bmp", argv[1],
				test, i);
			success = mh_renderer_update_mesh(renderer, 1) &&
				mh_renderer_scene_frame(renderer, projection,
					view, models, 0) &&
				mh_renderer_capture(renderer, path);
			t = 240.5 / 960;
			amount = left * (1 - t) + right * t;

			for (j = 0; j < 3; j++)
				expected[j] =
					cue[j] * (1 - amount) + sky[j] * amount;

			if (success)
				success = capture_matches(
					path, 240, 320, expected);
		}

		/*
		 * A fourth corner below .7 keeps BOTH constituent triangles in
		 * the ordinary pass. On triangle 0 all three corners are fully
		 * fogged.
		 */
		for (j = 0; j < 6; j++) {
			memcpy(f[1].vertices[j].position, xy[j], sizeof(xy[j]));
			f[1].vertices[j].position[2] = j == 5 ? .65f : .9f;
		}

		sprintf(path, "%s-whole-quad-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_update_mesh(renderer, 1) &&
				mh_renderer_scene_frame(
					renderer, identity, view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 720, 480, cue);

		mh_renderer_destroy(renderer);
	}

	/*
	 * Alpha lists follow opaque geometry even when supplied first. Later
	 * additive effects see their depth writes, but not the discarded holes.
	 */
	for (test = 0; test < 2 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .2f);
		make_quad(&f[2], green, .4f);
		f[1].draw.alpha_test = 1;
		f[1].image.width = 2;
		f[1].pixels[3] = 0;
		memcpy(f[1].pixels + 4, red, 3);
		f[1].pixels[7] = 255;

		for (j = 0; j < 6; j++)
			f[1].vertices[j].uv[0] = (xy[j][0] + 1) * .5f;

		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL && mh_renderer_mesh_sky(renderer, 0);

		if (test && success) {
			/*
			 * Unavailable pass samples texture slot zero, so green
			 * source is multiplied by a quarter-strength vertex
			 * color.
			 */
			for (j = 0; j < 6; j++)
				f[2].vertices[j].color[0] =
					f[2].vertices[j].color[1] =
						f[2].vertices[j].color[2] =
							.25f;

			success = mh_renderer_mesh_unavailable(
				renderer, 2, f[2].vertices);
		}

		sprintf(path, "%s-depth-%u.bmp", argv[1], test);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path);

		for (j = 0; j < 3; j++)
			expected[j] = test ? sky[j] + green[j] * .25 : green[j];

		if (success)
			success = capture_matches(path, 240, 320, expected);

		for (j = 0; j < 3; j++)
			expected[j] = red[j];

		if (success)
			success = capture_matches(path, 720, 320, expected);

		mh_renderer_destroy(renderer);
	}

	/*
	 * Fully faded solids still write depth; fully faded alpha faces fail
	 * GREATER/zero and must leave depth untouched for later effects.
	 */
	for (test = 0; test < 2 && success; test++) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .95f);
		make_quad(&f[2], green, .96f);
		f[1].draw.alpha_test = (int)test;

		for (j = 0; j < 6; j++)
			f[2].vertices[j].color[0] = f[2].vertices[j].color[1] =
				f[2].vertices[j].color[2] = .25f;

		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_set_fog(renderer, &fog) &&
			mh_renderer_mesh_unavailable(
				renderer, 2, f[2].vertices);
		sprintf(path, "%s-faded-depth-%u.bmp", argv[1], test);

		for (j = 0; j < 3; j++)
			expected[j] = sky[j] + (test ? green[j] * .25 : 0);

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);

		mh_renderer_destroy(renderer);
	}

	/*
	 * Equal-depth alpha faces are pushed to the front. With LEQUAL the
	 * first submitted face draws last and wins, as in reversed GEQUAL.
	 */
	if (success) {
		make_quad(&f[0], sky, .99f);
		make_quad(&f[1], red, .2f);
		make_quad(&f[2], green, .2f);
		f[1].draw.alpha_test = f[2].draw.alpha_test = 1;
		renderer = mh_sdl_renderer_create_scene(NULL, meshes, 3);
		success = renderer != NULL && mh_renderer_mesh_sky(renderer, 0);
		sprintf(path, "%s-bucket-order.bmp", argv[1]);

		for (j = 0; j < 3; j++)
			expected[j] = red[j];

		if (success)
			success = mh_renderer_scene_frame(renderer, identity,
					view, models, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, 480, 320, expected);

		mh_renderer_destroy(renderer);
	}

	if (success)
		success = check_clipped_attributes(argv[1]);

	if (!success)
		fprintf(stderr, "Material pixel qualification failed: %s\n",
			SDL_GetError());

	SDL_Quit();

	if (success)
		puts("Material cutouts, texture formats, sampling, distant fade, whole quads, pass order, depth and clipped attribute pixels passed");

	return !success;
}
