#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "interfaces/render/backend.h"
#include "tron_render.h"

static unsigned int capture_count;

static void
make_quad(struct mh_renderer_tron_face *f, enum mh_scene_pass pass, float left,
	float top, float right, float bottom, float depth, float rgb)
{
	unsigned int i, j;

	memset(f, 0, sizeof(*f));
	f->pass = pass;
	f->count = 4;
	f->smooth = 1;

	for (i = 0; i < 4; i++) {
		f->corners[i].xy[0] = i == 1 || i == 2 ? right : left;
		f->corners[i].xy[1] = i >= 2 ? bottom : top;
		f->corners[i].depth = depth;
		f->corners[i].reciprocal_w = 1;
		f->corners[i].uv[0] = i == 1 || i == 2 ? 1 : 0;
		f->corners[i].uv[1] = .25f;

		for (j = 0; j < 3; j++)
			f->corners[i].rgb[j] = rgb;

		f->corners[i].visibility = 255;
	}
}

static void
free_capture(struct mh_image *image)
{
	if (image != NULL)
		free(image->rgba);

	free(image);
}

static struct mh_image *
read_capture(struct mh_renderer *renderer, const char *prefix)
{
	char path[1024], suffix[32];
	size_t prefix_length, suffix_length;
	struct mh_image *image;
	struct mh_error error;

	sprintf(suffix, "-%u.bmp", capture_count++);

	if (prefix != NULL) {
		prefix_length = strlen(prefix);
		suffix_length = strlen(suffix);

		if (prefix_length > sizeof(path) - 1 - suffix_length)
			return NULL;

		memcpy(path, prefix, prefix_length);
		memcpy(path + prefix_length, suffix, suffix_length + 1);

		if (!mh_renderer_capture(renderer, path))
			return NULL;
	}

	image = calloc(1, sizeof(*image));

	if (image == NULL)
		return NULL;

	if (mh_renderer_read_pixels(renderer, image, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		free(image);
		return NULL;
	}

	printf("%s TRON capture %u\n", mh_renderer_name(renderer),
		capture_count - 1);

	return image;
}

static struct mh_renderer *
open_renderer(const struct mh_render_backend *backend, struct mh_window *window,
	const struct mh_mesh *mesh)
{
	struct mh_renderer *renderer;
	struct mh_error error;

	renderer = NULL;

	if (mh_renderer_open(backend, window, &mesh, 1, 960, 640, 0, &renderer,
		&error) != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return renderer;
}

static struct mh_image *
capture_frame(struct mh_renderer *renderer, const char *prefix,
	const struct mh_renderer_tron_face *faces, size_t count)
{
	struct mh_error error;

	if (!mh_renderer_tron_frame_ui(renderer, faces, count, NULL)) {
		mh_renderer_error(renderer, &error);
		fprintf(stderr, "%s TRON frame: %s\n",
			mh_renderer_name(renderer), error.message);
		return NULL;
	}

	return read_capture(renderer, prefix);
}

static int
pixel_matches(
	struct mh_image *surface, unsigned int x, unsigned int y, double blue)
{
	const unsigned char *pixel;

	if (surface == NULL)
		return 0;

	pixel = (const unsigned char *)surface->rgba +
		y * (surface->width * 4) + x * 4;

	if (pixel[0] || pixel[1] || fabs(pixel[2] - blue) > 1.1) {
		fprintf(stderr,
			"TRON capture %u at %u,%u: %u,%u,%u expected 0,0,%.6f\n",
			capture_count - 1, x, y, pixel[0], pixel[1], pixel[2],
			blue);
		return 0;
	}

	return 1;
}

static int
edge_matches(struct mh_image *surface, unsigned int x, unsigned int y,
	unsigned int expected)
{
	const unsigned char *pixel;
	unsigned int i, maximum;

	if (surface == NULL)
		return 0;

	maximum = 0;

	/*
	 * Native line coverage may choose either adjacent pixel at an integer
	 * edge. Assert presence/brightness, not a claimed Voodoo raster
	 * pattern.
	 */
	for (i = y - 1; i <= y + 1; i++) {
		pixel = (const unsigned char *)surface->rgba +
			i * (surface->width * 4) + x * 4;

		if (pixel[0] || pixel[1])
			return 0;

		if (pixel[2] > maximum)
			maximum = pixel[2];
	}

	if (maximum != expected) {
		fprintf(stderr, "TRON capture %u edge %u,%u: %u expected %u\n",
			capture_count - 1, x, y, maximum, expected);
		return 0;
	}

	return 1;
}

static int
check_scene(const struct mh_render_backend *backend, struct mh_window *window,
	const char *prefix)
{
	static const unsigned int corner[6] = {0, 1, 2, 0, 2, 3};
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	struct mh_vertex vertices[12], lit[12];
	float colors[36];
	struct mh_mesh mesh;
	struct mh_draw draws[2];
	struct mh_mesh_primitive primitives[2];
	struct mh_image textures[2];
	struct mh_projection projection;
	struct mh_renderer *renderer;
	struct mh_image *surface;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned int i, j, n, test;
	int success;
	double expected;
	struct mh_error error;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(draws, 0, sizeof(draws));
	memset(primitives, 0, sizeof(primitives));

	for (i = 0; i < 2; i++) {
		textures[i].width = textures[i].height = 1;
		textures[i].rgba = white;
		draws[i].first = i * 6;
		draws[i].count = 6;
		draws[i].texture = 1;
		primitives[i].first = i * 6;
		primitives[i].count = 6;
		primitives[i].draw = i;
		primitives[i].type = 6;
		mesh.lods[i].vertex_first = i * 6;
		mesh.lods[i].vertex_count = 6;
		mesh.lods[i].draw_first = mesh.lods[i].primitive_first = i;
		mesh.lods[i].draw_count = mesh.lods[i].primitive_count = 1;

		for (j = 0; j < 6; j++) {
			n = corner[j];
			vertices[i * 6 + j].position[0] =
				n == 1 || n == 2 ? 1 : -1;
			vertices[i * 6 + j].position[1] = n >= 2 ? -.5f : .5f;
			vertices[i * 6 + j].position[2] = -2;
			vertices[i * 6 + j].color[0] =
				vertices[i * 6 + j].color[1] =
					vertices[i * 6 + j].color[2] =
						(i ? 50 : 100) / 255.0f;
			colors[(i * 6 + j) * 3] = colors[(i * 6 + j) * 3 + 1] =
				colors[(i * 6 + j) * 3 + 2] = i ? 50 : 100;
		}
	}

	mesh.vertices = vertices;
	mesh.vertex_count = 12;
	mesh.material_rgb = colors;
	mesh.material_rgb_count = 12;
	mesh.draws = draws;
	mesh.draw_count = 2;
	mesh.primitives = primitives;
	mesh.primitive_count = 2;
	mesh.textures = textures;
	mesh.texture_count = 2;
	mesh.lod_count = 2;

	if (mh_projection_setup(960, 640, 1.5f, 1, .5f, 300, &projection,
		NULL) != MH_ERROR_NONE)
		return 0;

	renderer = open_renderer(backend, window, &mesh);

	if (renderer == NULL)
		return 0;

	success = 1;

	for (test = 0; test < 13 && success; test++) {
		expected = 99;
		mesh.lod = 0;
		mesh.lod_count = 2;
		draws[0].alpha_test = draws[1].alpha_test = 0;
		success = mh_renderer_mesh_visible(renderer, 0, 1) &&
			mh_renderer_update_mesh(renderer, 0);

		if (!success)
			break;

		if (test == 1) {
			/*
			 * Only the uploaded copy changes: catch stale CPU
			 * geometry or lighting.
			 */
			memcpy(lit, vertices, sizeof(lit));

			for (i = 0; i < 12; i++) {
				lit[i].position[0] += 1;

				for (j = 0; j < 3; j++)
					lit[i].color[j] = 200 / 255.0f;
			}

			success = mh_renderer_mesh_lit(renderer, 0, lit);
			expected = 198;
		} else if (test == 2) {
			mesh.lod = 1;
			expected = 49.5;
		} else if (test == 3) {
			success = mh_renderer_mesh_visible(renderer, 0, 0);
			expected = 0;
		} else if (test == 4) {
			success = mh_renderer_mesh_sky(renderer, 0);
			expected = 0;
		} else if (test == 5) {
			success = mh_renderer_mesh_skids(renderer, 0);
			expected = 0;
		} else if (test == 6) {
			success = mh_renderer_mesh_particles(renderer, 0);
			expected = 0;
		} else if (test == 7) {
			success = mh_renderer_mesh_halos(renderer, 0);
			expected = 0;
		} else if (test == 8) {
			success = mh_renderer_mesh_shadows(renderer, 0);
			expected = 0;
		} else if (test == 9) {
			/*
			 * Equal depth, same texture: original push-front order
			 * leaves face zero on top.
			 */
			mesh.lod_count = 0;
		} else if (test == 10) {
			/*
			 * Alpha must follow textured faces even when submitted
			 * first.
			 */
			mesh.lod_count = 0;
			draws[0].alpha_test = 1;
			expected = 24.75;
		} else if (test == 11) {
			mesh.lod_count = 0;
			success = mh_renderer_mesh_unavailable(
				renderer, 0, vertices);
			expected = 148.5;
		} else if (test == 12) {
			/*
			 * A near-straddling quad becomes two clipped polygons.
			 */
			memcpy(lit, vertices, sizeof(lit));

			for (i = 0; i < 6; i++)
				if (corner[i] == 0)
					lit[i].position[2] = -.2f;

			success = mh_renderer_mesh_lit(renderer, 0, lit);
		}

		if (success)
			success = mh_renderer_tron_scene_ui(
				renderer, &projection, identity, NULL, 1, NULL);

		if (!success)
			break;

		surface = read_capture(renderer, prefix);
		success = pixel_matches(surface, 600, 350, expected) &&
			pixel_matches(
				surface, 300, 300, test == 1 ? 0 : expected);
		free_capture(surface);
	}

	if (!success) {
		mh_renderer_error(renderer, &error);
		fprintf(stderr, "%s TRON scene case %u: %s\n",
			mh_renderer_name(renderer), test,
			error.message == NULL ? "pixel mismatch"
					: error.message);
	}

	mh_renderer_destroy(renderer);

	return success;
}

static int
check_fills(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int i, test;
	double amount;
	int success;

	success = 1;

	/*
	 * Blue fills ignore source RGB and alpha. Black fills retain only their
	 * blue perimeter: an internal quad diagonal must never become an edge.
	 */
	for (test = 0; test < 3 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 100);
		make_quad(&faces[1],
			test == 0	    ? MH_SCENE_SOLID
				: test == 1 ? MH_SCENE_TEXTURED
					: MH_SCENE_ENVIRONMENT,
			100, 100, 300, 300, .7f, 100);

		if (test == 2)
			for (i = 0; i < 4; i++)
				faces[1].corners[i].visibility = 128;

		surface = capture_frame(renderer, prefix, faces, 2);
		amount = test == 0 ? 0 : test == 1 ? 99 : 99 * 128.0 / 255;
		success = pixel_matches(surface, 200, 200, amount) &&
			pixel_matches(surface, 125, 200, amount) &&
			pixel_matches(surface, 275, 200, amount) &&
			edge_matches(surface, 200, 100, test == 2 ? 128 : 255);
		free_capture(surface);
	}

	return success;
}

static int
check_alpha(
	struct mh_renderer *renderer, const char *prefix, double texture_alpha)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int i, test;
	int success;

	success = 1;

	/*
	 * Destination attenuation versus alpha-tested replacement, with both
	 * zero and partial texture alpha. Also test zero-opacity depth writes.
	 */
	for (test = 0; test < 4 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 100);
		make_quad(&faces[1],
			test == 0 || test == 3 ? MH_SCENE_DISTANT_SOLID
				: test == 1    ? MH_SCENE_DISTANT_ALPHA
					: MH_SCENE_ALPHA,
			100, 100, 500, 500, .8f, 100);

		for (i = 0; i < 4; i++)
			faces[1].corners[i].background = test == 3 ? 255 : 128;

		make_quad(&faces[2], MH_SCENE_UNAVAILABLE, 100, 100, 500, 500,
			.4f, 50);
		surface = capture_frame(
			renderer, prefix, faces, test == 3 ? 3 : 2);
		success = pixel_matches(surface, 150, 300,
				test == 0 ? 99 * 128.0 / 255 : 99) &&
			pixel_matches(surface, 250, 300,
				test == 0	    ? 99 * 128.0 / 255
					: test == 1 ? 99 *
						(1 -
							127.0 * texture_alpha /
								65025)
					: test == 2 ? 24.75
						: 99) &&
			pixel_matches(surface, 350, 300,
				test < 2	    ? 99 * 128.0 / 255
					: test == 2 ? 24.75
						: 99);
		free_capture(surface);
	}

	for (test = 0; test < 2 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 20);
		make_quad(&faces[1],
			test ? MH_SCENE_ALPHA : MH_SCENE_DISTANT_ALPHA, 100,
			100, 500, 500, .8f, 100);

		for (i = 0; i < 4; i++)
			faces[1].corners[i].background = 255;

		make_quad(&faces[2], MH_SCENE_UNAVAILABLE, 100, 100, 500, 500,
			.4f, 50);
		surface = capture_frame(renderer, prefix, faces, 3);
		success = pixel_matches(surface, 150, 300, 69.3) &&
			pixel_matches(surface, 350, 300, test ? 24.75 : 69.3);
		free_capture(surface);
	}

	return success;
}

static int
check_additive_depth(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int test;
	int success;

	success = 1;

	/*
	 * Unlike ordinary unavailable rendering, TRON's additive pass writes
	 * depth. Changing near/far submission order must change accumulation.
	 */
	for (test = 0; test < 2 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 20);
		make_quad(&faces[1], MH_SCENE_UNAVAILABLE, 100, 100, 500, 500,
			test ? .4f : .8f, 30);
		make_quad(&faces[2], MH_SCENE_UNAVAILABLE, 100, 100, 500, 500,
			test ? .8f : .4f, 60);
		surface = capture_frame(renderer, prefix, faces, 3);
		success = pixel_matches(surface, 250, 300, test ? 108.9 : 49.5);
		free_capture(surface);
	}

	return success;
}

static int
check_outline_bias(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int test;
	int success;

	success = 1;

	/*
	 * A foreground face 20 reversed-Z units nearer blocks bias 10, while
	 * a bias-30 edge remains visible. The fill stays behind in both cases.
	 */
	for (test = 0; test < 2 && success; test++) {
		make_quad(&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640,
			.5f + 20.0f / 65536, 100);
		make_quad(&faces[1], test ? MH_SCENE_TEXTURED : MH_SCENE_SOLID,
			100, 100, 500, 500, .5f, 200);
		surface = capture_frame(renderer, prefix, faces, 2);
		success = pixel_matches(surface, 250, 300, 99) &&
			edge_matches(surface, 250, 100, test ? 255 : 99);
		free_capture(surface);
	}

	return success;
}

static int
check_perspective(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int i, test;
	double amount, x, reciprocal_w, u;
	int success;

	success = 1;

	/*
	 * Unequal W must affect texture cutouts but not color/fog
	 * interpolation. The near-plane/viewport GPU rasterizer is not the
	 * original CPU clipper.
	 */
	for (test = 0; test < 2 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 20);
		make_quad(&faces[1], test ? MH_SCENE_ALPHA : MH_SCENE_TEXTURED,
			0, 0, 960, 640, .7f, 100);

		for (i = 0; i < 4; i++) {
			faces[1].corners[i].reciprocal_w =
				i == 1 || i == 2 ? .125f : 1;

			if (!test)
				faces[1].corners[i].visibility =
					i == 1 || i == 2 ? 0 : 255;
		}

		surface = capture_frame(renderer, prefix, faces, 2);

		for (i = 1; i < 8 && success; i++) {
			x = (i * 120 + .5) / 960;
			reciprocal_w = 1 - x + x * .125;
			u = x * .125 / reciprocal_w;
			amount = test ? (u < .25 ? 19.8 : 24.75) : 99 * (1 - x);
			success = pixel_matches(surface, i * 120, 320, amount);
		}

		free_capture(surface);
	}

	return success;
}

static int
check_wrapping(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	unsigned int i, test;
	int success;

	success = 1;

	/* Both axes wrap, including negative coordinates. */
	for (test = 0; test < 4 && success; test++) {
		make_quad(
			&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 20);
		make_quad(&faces[1], MH_SCENE_ALPHA, 100, 100, 500, 500, .8f,
			100);

		for (i = 0; i < 4; i++)
			faces[1].corners[i].uv[test / 2] += test % 2 ? -1 : 1;

		surface = capture_frame(renderer, prefix, faces, 2);
		success = pixel_matches(surface, 150, 300, 19.8) &&
			pixel_matches(surface, 250, 300, 24.75);
		free_capture(surface);
	}

	return success;
}

static int
check_invalid_frames(struct mh_renderer *renderer, const char *prefix)
{
	struct mh_renderer_tron_face faces[3];
	struct mh_image *surface;
	struct mh_renderer_tron_face saved;
	int success;

	success = 1;
	make_quad(&faces[0], MH_SCENE_TEXTURED, 0, 0, 960, 640, .1f, 20);

	/*
	 * Reject malformed frames before submitting anything; a subsequent
	 * valid frame can reuse the renderer. Empty frames clear stale world
	 * pixels.
	 */
	saved = faces[0];
	faces[0].texture = 99;
	success = !mh_renderer_tron_frame_ui(renderer, faces, 1, NULL);
	faces[0] = saved;
	faces[0].count = 5;
	success =
		success && !mh_renderer_tron_frame_ui(renderer, faces, 1, NULL);
	faces[0] = saved;
	faces[0].corners[3].reciprocal_w = 1e-30f;
	success =
		success && !mh_renderer_tron_frame_ui(renderer, faces, 1, NULL);
	faces[0] = saved;
	success = success &&
		!mh_renderer_tron_frame_ui(renderer, NULL, 1, NULL) &&
		!mh_renderer_tron_frame_ui(renderer, faces, 1000001, NULL);
	surface = capture_frame(renderer, prefix, faces, 1);
	success = success && pixel_matches(surface, 250, 300, 19.8);
	free_capture(surface);
	surface = capture_frame(renderer, prefix, NULL, 0);
	success = success && pixel_matches(surface, 250, 300, 0);
	free_capture(surface);

	return success;
}

int
mh_tron_render_check(const struct mh_render_backend *backend,
	struct mh_window *window, const char *prefix)
{
	struct mh_vertex vertices[3];
	struct mh_mesh mesh;
	struct mh_draw draw;
	struct mh_image texture;
	struct mh_renderer *renderer;
	unsigned char pixels[32 * 32 * 4], format;
	unsigned char texels[32] = {255, 0, 0, 0, 0, 255, 0, 128, 255, 0, 255,
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 0, 255, 0, 0, 128,
		0, 0, 0, 255};

	unsigned int x_pixel, y_pixel;
	int success;
	double texture_alpha;

	capture_count = 0;

	memset(vertices, 0, sizeof(vertices));
	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	draw.count = 3;
	draw.prelit = 1;

	/*
	 * PowerSGL's minimum size is 32; preserve the same eight cutout cells.
	 */
	for (y_pixel = 0; y_pixel < 32; y_pixel++)
		for (x_pixel = 0; x_pixel < 32; x_pixel++)
			memcpy(pixels + (y_pixel * 32 + x_pixel) * 4,
				texels + ((y_pixel / 16) * 4 + x_pixel / 8) * 4,
				4);

	texture.width = texture.height = 32;
	texture.rgba = pixels;
	mesh.vertices = vertices;
	mesh.vertex_count = 3;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.textures = &texture;
	mesh.texture_count = 1;
	texture_alpha = 128;

	if (backend->profile == MH_RENDER_POWERVR) {
		format = MH_TEXTURE_ARGB4444;
		mesh.texture_formats = &format;
		mesh.texture_format_count = 1;
		texture_alpha = 136;
	}

	renderer = open_renderer(backend, window, &mesh);
	success = renderer != NULL;

	if (success)
		success = check_fills(renderer, prefix) &&
			check_alpha(renderer, prefix, texture_alpha) &&
			check_additive_depth(renderer, prefix) &&
			check_outline_bias(renderer, prefix) &&
			check_perspective(renderer, prefix) &&
			check_wrapping(renderer, prefix) &&
			check_invalid_frames(renderer, prefix);

	mh_renderer_destroy(renderer);

	if (success)
		success = check_scene(backend, window, prefix);

	if (success)
		printf("%u TRON captures: projected faces, live geometry/lighting, LODs, pass order, effect suppression and near splitting pass\n",
			capture_count);

	return !success;
}
