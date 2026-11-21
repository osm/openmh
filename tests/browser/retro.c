#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/render/matrix.h"
#include "interfaces/render/backend.h"
#include "renderer_depth.h"
#include "retro.h"

static struct mh_retro_draw retro;
static struct mh_ui_list ui;
static unsigned int failures;

static void
check(int success, const char *label)
{
	if (success)
		printf("%s\n", label);
	else {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	}
}

static void
quad(unsigned int first, float left, float top, float right, float bottom,
	float depth, float red, float green, float blue, float alpha, float u)
{
	static const unsigned int corners[6][2] = {
		{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};

	struct mh_retro_draw_vertex *vertex;
	unsigned int i;

	for (i = 0; i < 6; i++) {
		vertex = &retro.vertices[first + i];
		vertex->vertex.xy[0] = corners[i][0] ? right : left;
		vertex->vertex.xy[1] = corners[i][1] ? bottom : top;
		vertex->vertex.depth = depth;
		vertex->vertex.rgb[0] = red;
		vertex->vertex.rgb[1] = green;
		vertex->vertex.rgb[2] = blue;
		vertex->vertex.alpha = alpha;
		vertex->uv[0] = u;
		vertex->uv[1] = .5f;
	}
}

static int
frame(struct mh_renderer *renderer, const struct mh_projection *projection)
{
	static const float matrix[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -1, 0};

	struct mh_error error;
	int success;

	if (mh_renderer_profile(renderer) == MH_RENDER_GLIDE) {
		success = mh_renderer_scene_frame_retro(renderer, matrix,
			mh_render_identity, NULL, 0, &ui, &retro);
		mh_renderer_error(renderer, &error);
	} else
		success = mh_renderer_scene_frame_lens(renderer, projection,
				mh_render_identity, NULL, 0, &ui, &retro,
				NULL, &error) == MH_ERROR_NONE;

	if (!success)
		fprintf(stderr, "retro frame: %s\n", error.message);

	return success;
}

static int
pixel(struct mh_renderer *renderer, unsigned int x, unsigned int y, int red,
	int green, int blue, int alpha)
{
	struct mh_image image;
	struct mh_error error;
	const unsigned char *p;
	int success;

	if (mh_renderer_read_pixels(renderer, &image, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 0;
	}

	p = image.rgba + ((size_t)y * image.width + x) * 4;
	success = abs(p[0] - red) <= 1 && abs(p[1] - green) <= 1 &&
		abs(p[2] - blue) <= 1 && abs(p[3] - alpha) <= 1;

	if (!success)
		fprintf(stderr,
			"retro pixel %u,%u: %u,%u,%u,%u expected %d,%d,%d,%d\n",
			x, y, p[0], p[1], p[2], p[3], red, green, blue, alpha);

	free(image.rgba);

	return success;
}

static void
rejected_frames(struct mh_renderer *renderer)
{
	struct mh_error error;

	retro.width++;
	check(!mh_renderer_scene_frame_retro(
		renderer, NULL, NULL, NULL, 0, NULL, &retro) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_INVALID,
		"retro rejects a frame with the wrong viewport");
	retro.width--;
	retro.text_end = 31;
	check(!mh_renderer_scene_frame_retro(
		renderer, NULL, NULL, NULL, 0, NULL, &retro) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_INVALID,
		"retro rejects partial text quads before drawing");
	retro.text_end = 42;
	retro.vertices[0].vertex.depth = 65537;
	check(!mh_renderer_scene_frame_retro(
		renderer, NULL, NULL, NULL, 0, NULL, &retro) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_INVALID,
		"retro rejects vertex depth outside its original range");
	retro.vertices[0].vertex.depth = 8192;
}

unsigned int
mh_webgl_retro_check(struct mh_renderer *renderer)
{
	unsigned char texels[12] = {
		255, 255, 255, 255, 0, 0, 255, 128, 255, 255, 255, 0};

	const float yellow[4] = {1, 1, 0, 1};
	struct mh_ui_atlas atlas;
	struct mh_ui_region solid;
	struct mh_projection projection;
	struct mh_depth_probe probes[4] = {{1, 80, 100, .24f},
		{1, 80, 100, .26f}, {1, 240, 100, .24f}, {1, 240, 100, .26f}};

	unsigned char visible[4] = {0, 1, 0, 1};
	unsigned int i;

	failures = 0;
	memset(&retro, 0, sizeof(retro));
	memset(&atlas, 0, sizeof(atlas));
	memset(&solid, 0, sizeof(solid));
	atlas.image.width = 3;
	atlas.image.height = 1;
	atlas.image.rgba = texels;
	solid.width = solid.height = 1;
	mh_ui_begin(&ui, 320, 200);

	if (!mh_renderer_ui_upload(renderer, &atlas) ||
		!mh_ui_quad_scaled(
			&ui, &atlas, &solid, 0, 0, 320, 200, yellow) ||
		mh_projection_setup(320, 200, 4.0f / 3, 1, 1, 100, &projection,
			NULL) != MH_ERROR_NONE) {
		check(0, "retro fixture setup");
		return failures;
	}

	retro.width = 320;
	retro.height = 200;
	retro.text_end = 42;
	retro.count = 48;

	/*
	 * Backdrop lies behind the world. The other four quads are degenerate.
	 */
	quad(0, 0, 0, 320, 200, 8192, 255, 0, 0, 128, 0);
	quad(30, 0, 0, 160, 150, 49152, 255, 255, 255, 255, .5f);
	quad(36, 160, 0, 320, 150, 49152, 255, 255, 255, 255, .833333f);
	quad(42, 0, 0, 320, 200, 32768, 0, 255, 0, 255, 0);
	rejected_frames(renderer);

	check(frame(renderer, &projection) &&
			pixel(renderer, 80, 100, 127, 63, 128, 128),
		"retro backdrop ignores world depth and font blends over the HUD");
	check(pixel(renderer, 240, 100, 255, 127, 0, 0),
		"transparent retro font texels retain depth writes and replace alpha");
	check(pixel(renderer, 240, 175, 0, 255, 0, 255),
		"retro geometry draws untextured outside the font region");
	check(mh_renderer_depth_queue(renderer, probes, 4) &&
			mh_webgl_test_read_depth(renderer, visible, 4) &&
			visible[0] == 1 && visible[1] == 0 && visible[2] == 1 &&
			visible[3] == 0,
		"retro depth is retained in the completed frame including transparent text");

	for (i = 42; i < 48; i++)
		retro.vertices[i].vertex.depth = 49152;

	check(frame(renderer, &projection) &&
			pixel(renderer, 80, 100, 0, 255, 0, 255) &&
			pixel(renderer, 240, 100, 0, 255, 0, 255),
		"retro geometry accepts equal text depth in submission order");

	retro.count = retro.text_end = 30;
	check(mh_renderer_scene_frame_retro(
		renderer, NULL, NULL, NULL, 0, &ui, &retro) &&
			pixel(renderer, 80, 100, 255, 127, 0, 128),
		"retro supports backdrop-only UI frames without text or meshes");
	check(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &ui) &&
			pixel(renderer, 80, 100, 255, 255, 0, 255),
		"normal UI rendering restores state after a retro pass");

	return failures;
}
