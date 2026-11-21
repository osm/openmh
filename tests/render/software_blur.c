#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_blur.h"
#include "backends/render/software_provider.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"
#ifdef MH_TEST_PRESENT_FAILURE
#include "../support/software_presentation.h"
#endif

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_ui_list overlay;

static int
check_padded_rows(void)
{
	struct mh_software_surface surface;
	unsigned char pixels[24], history[24];
	unsigned int x, y;

	memset(&surface, 0, sizeof(surface));
	memset(pixels, 0xcc, sizeof(pixels));
	memset(history, 0, sizeof(history));
	surface.pixels = pixels;
	surface.width = 4;
	surface.height = 2;
	surface.pitch = 12;
	surface.size = sizeof(pixels);
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;

	for (y = 0; y < 2; y++)
		memset(pixels + y * 12, 255, 8);

	CHECK(mh_software_blur(&surface, history, sizeof(history), NULL) ==
		MH_ERROR_NONE);

	for (y = 0; y < 2; y++) {
		for (x = 0; x < 4; x++)
			CHECK(mh_read_u16le(pixels + y * 12 + x * 2) == 0x7bef);

		for (x = 8; x < 12; x++)
			CHECK(pixels[y * 12 + x] == 0xcc);
	}

	return 0;
}

static int
check_center(struct mh_renderer *renderer, unsigned int expected)
{
	struct mh_file picture, encoded;
	struct mh_image image;
	struct mh_picture view;
	unsigned int pixel;

	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(mh_renderer_read_pixels(renderer, &image, NULL) == MH_ERROR_NONE);
	memset(&view, 0, sizeof(view));
	view.format = MH_PICTURE_RGBA8;
	view.width = image.width;
	view.height = image.height;
	view.pixels = image.rgba;
	view.pixel_size = (size_t)image.width * image.height * 4;
	CHECK(mh_image_tga_encode(&view, &encoded, NULL) == MH_ERROR_NONE);
	CHECK(encoded.size == picture.size &&
		memcmp(encoded.data, picture.data, picture.size) == 0);
	free(encoded.data);
	free(image.rgba);
	pixel = mh_read_u16le(picture.data + MH_TGA_HEADER_SIZE +
		((199 - 100) * 320 + 160) * 2);
	free(picture.data);

	if (pixel != expected)
		fprintf(stderr, "center %04x, expected %04x\n", pixel,
			expected);

	CHECK(pixel == expected);

	return 0;
}

#ifndef MH_TEST_PRESENT_FAILURE
static int
check_row_output(void)
{
	struct mh_renderer *renderer;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	struct mh_file picture;
	unsigned char rgba[4];
	float matrix[16], white[4];
	unsigned int i, x, y, expected;

	memset(&atlas, 0, sizeof(atlas));
	memset(&region, 0, sizeof(region));
	memset(matrix, 0, sizeof(matrix));
	memset(rgba, 255, sizeof(rgba));
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;

	for (i = 0; i < 4; i++)
		white[i] = 1;

	atlas.image.width = atlas.image.height = 1;
	atlas.image.rgba = rgba;
	region.width = region.height = 1;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 321, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&overlay, 321, 200);
	CHECK(mh_ui_quad_scaled(
		&overlay, &atlas, &region, 0, 0, 321, 200, white));
	CHECK(mh_renderer_ui_frame(renderer, &overlay));
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_scene_frame(renderer, matrix, matrix, NULL, 0));
	CHECK(mh_renderer_tga_capture(renderer, &picture));

	/*
	 * Each row's unpaired display pixel survives, not just the frame end.
	 */
	for (y = 0; y < 200; y++) {
		for (x = 0; x < 321; x++) {
			expected = x == 320 ? 0x7fff : 0;
			CHECK(mh_read_u16le(picture.data + MH_TGA_HEADER_SIZE +
				((size_t)y * 321 + x) * 2) == expected);
		}
	}

	free(picture.data);
	mh_renderer_destroy(renderer);

	return 0;
}
#endif

static int
check_lifecycle(void)
{
	struct mh_renderer *renderer;
#ifdef MH_TEST_PRESENT_FAILURE
	struct mh_test_presentation output, other;
	struct mh_renderer *independent, *unchanged;
	unsigned int frames;
#endif
	struct mh_mesh mesh;
	const struct mh_mesh *source;
	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	struct mh_projection projection;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	float view[16], white[4];
	unsigned char rgba[4];
	unsigned int i;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&atlas, 0, sizeof(atlas));
	memset(&region, 0, sizeof(region));
	memset(view, 0, sizeof(view));
	view[0] = view[5] = view[15] = 1;
	view[10] = -1;
	vertices[0].position[0] = 15;
	vertices[1].position[0] = -15;
	vertices[0].position[1] = vertices[1].position[1] = -10;
	vertices[2].position[1] = 12;

	for (i = 0; i < 3; i++) {
		vertices[i].position[2] = 70;
		vertices[i].color[0] = 1;
	}

	for (i = 0; i < 4; i++) {
		white[i] = 1;
		rgba[i] = 255;
	}

	draw.prelit = 1;
	draw.count = primitive.count = mesh.vertex_count = 3;
	mesh.vertices = vertices;
	mesh.draws = &draw;
	mesh.draw_count = mesh.primitive_count = 1;
	mesh.primitives = &primitive;
	source = &mesh;
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 100, &projection,
		NULL) == MH_ERROR_NONE);
#ifdef MH_TEST_PRESENT_FAILURE
	CHECK(mh_test_presentation_open(&output, NULL) == MH_ERROR_NONE);
	CHECK(mh_test_presentation_open(&other, NULL) == MH_ERROR_NONE);
	other.output.capture = NULL;
	unchanged = NULL;
	CHECK(mh_renderer_open(mh_software_backend(), output.window, &source, 1,
		320, 200, 0, &unchanged, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(unchanged == NULL && output.opened == 0);
	output.fail_open = 1;
	CHECK(mh_renderer_open(&output.backend, output.window, &source, 1, 320,
		200, 0, &unchanged, NULL) == MH_ERROR_STATE);
	CHECK(unchanged == NULL && output.closed == 0 && output.pixels == NULL);
	output.fail_open = 0;
	CHECK(mh_renderer_open(&output.backend, output.window, &source, 1, 320,
		200, 0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(&other.backend, other.window, NULL, 0, 320, 200,
		0, &independent, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_ui_frame(independent, NULL));
	CHECK(other.frames == 1 && output.frames == 0);
	CHECK(!mh_renderer_capture(independent, "memory-frame"));
#else
	CHECK(mh_renderer_open(mh_software_backend(), NULL, &source, 1, 320,
		200, 0, &renderer, NULL) == MH_ERROR_NONE);
#endif
	atlas.image.width = atlas.image.height = 1;
	atlas.image.rgba = rgba;
	region.width = region.height = 1;
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&overlay, 320, 200);
	CHECK(mh_ui_quad_scaled(
		&overlay, &atlas, &region, 150, 90, 20, 20, white));
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		&overlay, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 0x7fff) == 0);
#ifdef MH_TEST_PRESENT_FAILURE
	CHECK(output.pixels[(100 * 320 + 160) * 4] == 255 &&
		output.pixels[(100 * 320 + 160) * 4 + 1] == 255 &&
		output.pixels[(100 * 320 + 160) * 4 + 2] == 255 &&
		output.pixels[(100 * 320 + 160) * 4 + 3] == 255);
	CHECK(mh_renderer_capture(renderer, "memory-frame"));
	CHECK(output.captures == 1 && output.frames == 1 && other.frames == 1);
#endif

	/*
	 * Repeated enable preserves history; the previous white HUD does not.
	 */
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 23U << 10) == 0);
	CHECK(mh_renderer_mesh_visible(renderer, 0, 0));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 11U << 10) == 0);

	/* Results replace the display but do not become race history. */
	CHECK(mh_renderer_ui_frame(renderer, &overlay));
	CHECK(check_center(renderer, 0x7fff) == 0);
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 5U << 10) == 0);
	CHECK(mh_renderer_set_motion_blur(renderer, 0));
	CHECK(mh_renderer_mesh_visible(renderer, 0, 1));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 31U << 10) == 0);
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 15U << 10) == 0);

#ifdef MH_TEST_PRESENT_FAILURE
	/* Window failure happens after drawing, before history publication. */
	frames = output.frames;
	output.fail_present = 1;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		&overlay, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(check_center(renderer, 15U << 10) == 0);
	CHECK(output.frames == frames && other.frames == 1);
	output.fail_present = 0;
	output.fail_draw = 1;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		&overlay, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(check_center(renderer, 15U << 10) == 0 &&
		output.frames == frames);
	output.fail_draw = 0;
#endif

	/* A failed scene must not advance or corrupt accumulated history. */
	vertices[1].color[1] = 1;
	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(check_center(renderer, 15U << 10) == 0);
	vertices[1].color[1] = 0;
	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_center(renderer, 23U << 10) == 0);
	mh_renderer_destroy(renderer);
#ifdef MH_TEST_PRESENT_FAILURE
	CHECK(output.opened == 1 && output.closed == 1 &&
		output.pixels == NULL);
	CHECK(other.closed == 0 && other.references == 1);
	mh_renderer_destroy(independent);
	mh_test_presentation_close(&output);
	mh_test_presentation_close(&other);
	CHECK(output.references == 0 && other.references == 0 &&
		other.closed == 1);
#endif

	return 0;
}

int
main(void)
{

	CHECK(check_padded_rows() == 0);
#ifndef MH_TEST_PRESENT_FAILURE
	CHECK(check_row_output() == 0);
#endif
	CHECK(check_lifecycle() == 0);
	puts("Software scene history excludes HUD/results and survives failed frames");

	return 0;
}
