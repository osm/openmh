#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "backends/render/software_ui.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/spr.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_ui_list list;
static const float white[4] = {1, 1, 1, 1};
static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static unsigned int
picture_pixel(const struct mh_file *picture, unsigned int x, unsigned int y)
{
	unsigned int width, height;

	width = mh_read_u16le(picture->data + 12);
	height = mh_read_u16le(picture->data + 14);

	return mh_read_u16le(picture->data + 18 +
		((size_t)(height - 1 - y) * width + x) * 2);
}

static int
check_retained_frame(struct mh_renderer *renderer, const struct mh_file *before)
{
	struct mh_file picture;

	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(picture.size == before->size &&
		memcmp(picture.data, before->data, picture.size) == 0);
	free(picture.data);

	return 0;
}

static int
fixtures(void)
{
	struct mh_renderer *renderer;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region, solid;
	struct mh_projection projection;
	struct mh_ui_vertex swap;
	struct mh_file picture, before;
	float tint[4], view[16];
	unsigned int i, x, y, saved;

	memset(&atlas, 0, sizeof(atlas));
	atlas.image.width = atlas.image.height = 4;
	atlas.image.rgba = calloc(16, 4);
	CHECK(atlas.image.rgba != NULL);
	atlas.image.rgba[0] = atlas.image.rgba[3] = 255;
	atlas.image.rgba[7] = 255;	       /* Opaque black beside red. */
	memset(atlas.image.rgba + 8, 255, 4);  /* Solid white. */
	memset(atlas.image.rgba + 16, 255, 3); /* Transparent white. */
	atlas.image.rgba[21] = atlas.image.rgba[23] = 255; /* Green. */
	region.x = region.y = 0;
	region.width = region.height = 2;
	solid.x = 2;
	solid.y = 0;
	solid.width = solid.height = 1;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	mh_ui_begin(&list, 320, 200);
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(!mh_renderer_ui_upload(renderer, NULL));
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	CHECK(!mh_renderer_ui_upload(renderer, &atlas));
	tint[0] = tint[1] = 0;
	tint[2] = tint[3] = 1;
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, 0, 0, 4, 4, tint));
	CHECK(mh_ui_quad(&list, &atlas, &region, 1, 1, white));
	tint[0] = tint[1] = tint[2] = 1;
	tint[3] = .5f;
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, 10, 10, 4, 4, tint));
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &before));
	CHECK(picture_pixel(&before, 1, 1) == 0x7c00);
	CHECK(picture_pixel(&before, 2, 1) == 0);
	CHECK(picture_pixel(&before, 1, 2) == 0x1f);
	CHECK(picture_pixel(&before, 2, 2) == 0x3e0);

	for (y = 10; y < 14; y++)
		for (x = 10; x < 14; x++)
			CHECK(picture_pixel(&before, x, y) == 0x4210);

	CHECK(picture_pixel(&before, 14, 13) == 0 &&
		picture_pixel(&before, 13, 14) == 0);

	/*
	 * Reversed winding covers exactly the same pixels, including the seam.
	 */
	for (i = 0; i < list.count; i += 3) {
		swap = list.vertices[i + 1];
		list.vertices[i + 1] = list.vertices[i + 2];
		list.vertices[i + 2] = swap;
	}

	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	memset(atlas.image.rgba, 0, 64);
	free(atlas.image.rgba);
	atlas.image.rgba = NULL;
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	memcpy(view, identity, sizeof(view));
	view[10] = -1;
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 100, &projection,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		&list, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_retained_frame(renderer, &before) == 0);

	/*
	 * A late bad vertex must reject even the earlier valid quads
	 * atomically.
	 */
	swap = list.vertices[list.count - 1];
	list.vertices[list.count - 1].uv[1] = -1;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		&list, NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.vertices[list.count - 1] = swap;
	list.vertices[list.count - 1].position[0] = FLT_MAX;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.vertices[list.count - 1] = swap;
	list.vertices[list.count - 1].color[3] = 2;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.vertices[list.count - 1] = swap;
	saved = list.count;
	list.count--;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	list.count = MH_UI_MAX_QUADS * 6 + 6;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	list.count = saved;
	list.width++;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.width--;
	free(before.data);
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &region, -2, -2, 4, 4, white));
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &picture));

	for (y = 0; y < 3; y++)
		for (x = 0; x < 3; x++)
			CHECK(picture_pixel(&picture, x, y) ==
				(x < 2 && y < 2 ? 0x3e0U : 0));

	free(picture.data);
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, 10, 10, 4, 4, white));

	for (i = 0; i < list.count; i++)
		list.vertices[i].color[3] = i == 0 || i == 1 || i == 3 ? 0 : 1;

	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &picture));

	for (y = 0; y < 4; y++) {
		saved = y == 0 ? 4 : y == 1 ? 12 : y == 2 ? 19 : 27;

		for (x = 10; x < 14; x++)
			CHECK(picture_pixel(&picture, x, 10 + y) ==
				((saved << 10) | (saved << 5) | saved));
	}

	free(picture.data);
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, .5f, .5f, 2, 2, white));
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(picture_pixel(&picture, 0, 0) == 0x7fff &&
		picture_pixel(&picture, 1, 1) == 0x7fff);
	CHECK(picture_pixel(&picture, 2, 1) == 0 &&
		picture_pixel(&picture, 1, 2) == 0);
	free(picture.data);
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &picture));

	for (i = 18; i < picture.size; i++)
		CHECK(picture.data[i] == 0);

	free(picture.data);
	mh_renderer_destroy(renderer);

	return 0;
}

static int
scene_overlay(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	const struct mh_mesh *meshes[1];
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	struct mh_renderer *renderer;
	struct mh_file image;
	struct mh_file board;
	unsigned char rgba[4];
	unsigned int i;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&atlas, 0, sizeof(atlas));

	for (i = 0; i < 6; i++) {
		memcpy(vertices[i].position, xy[i], sizeof(xy[i]));
		vertices[i].position[2] = .5f;
		vertices[i].color[2] = 1;
	}

	mesh.vertices = vertices;
	mesh.vertex_count = draw.count = primitive.count = 6;
	mesh.draws = &draw;
	mesh.primitives = &primitive;
	mesh.draw_count = mesh.primitive_count = 1;
	draw.prelit = 1;
	primitive.type = 1;
	meshes[0] = &mesh;
	atlas.image.width = atlas.image.height = 1;
	atlas.image.rgba = rgba;
	rgba[0] = rgba[3] = 255;
	rgba[1] = rgba[2] = 0;
	region.x = region.y = 0;
	region.width = region.height = 1;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_ui_quad_scaled(
		&list, &atlas, &region, 100, 60, 20, 20, white));
	CHECK(mh_renderer_scene_frame_ui(
		renderer, identity, identity, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &image));
	CHECK(picture_pixel(&image, 110, 70) == 0x7c00);
	CHECK(picture_pixel(&image, 90, 70) == 0x1f);
	free(image.data);
	CHECK(mh_renderer_ui_frame(renderer, &list));
	CHECK(mh_renderer_tga_capture(renderer, &board));
	CHECK(picture_pixel(&board, 110, 70) == 0x7c00);
	CHECK(picture_pixel(&board, 90, 70) == 0);
	list.vertices[list.count - 1].color[3] = 2;
	CHECK(!mh_renderer_ui_frame(renderer, &list));
	CHECK(check_retained_frame(renderer, &board) == 0);
	list.vertices[list.count - 1].color[3] = 1;
	free(board.data);
	CHECK(mh_renderer_scene_frame_ui(
		renderer, identity, identity, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &image));
	CHECK(picture_pixel(&image, 90, 70) == 0x1f);
	free(image.data);
	mh_renderer_destroy(renderer);

	return 0;
}

static int
word_text(void)
{
	struct mh_font_program font;
	struct mh_ui_atlas atlas;
	struct mh_ui_region solid;
	struct mh_renderer *renderer;
	struct mh_file before;
	unsigned char mask[768], rgba[4] = {255, 255, 255, 255};
	unsigned char text[2] = {'A', 'A'};
	const float blue[4] = {0, 0, 1, 1};
	const float green[4] = {0, 1, 0, 1};
	unsigned int i, saved_size;

	memset(&font, 0, sizeof(font));
	memset(mask, 0, sizeof(mask));
	font.bytes = mask;
	font.cell_width = 3;
	font.height = 1;
	font.width = 48;
	font.rows = 16;
	font.advance['A'] = 3;
	mask[195] = mask[197] = MH_FONT_LOW;
	mask[196] = MH_FONT_HIGH;
	memset(&atlas, 0, sizeof(atlas));
	atlas.image.width = atlas.image.height = 1;
	atlas.image.rgba = rgba;
	solid.x = solid.y = 0;
	solid.width = solid.height = 1;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, 0, 0, 10, 10, blue));
	CHECK(mh_ui_word_text(&list, &font, text, 2, 1, 1, 0xffff,
		MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &solid, 2, 1, 1, 1, green));
	CHECK(mh_ui_word_text(&list, &font, text, 1, 5, 1, 0xffff,
		MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);
	text[0] = text[1] = 0; /* Commands own their copied character bytes. */
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &before));
	CHECK(picture_pixel(&before, 0, 1) == 0x1f);
	CHECK(picture_pixel(&before, 1, 1) == 0x7fff);
	CHECK(picture_pixel(&before, 2, 1) == 0x3e0);
	CHECK(picture_pixel(&before, 3, 1) == 0x7fff);
	CHECK(picture_pixel(&before, 5, 1) == 0x7fff);

	/* The final half-written word retains the background's high byte. */
	CHECK(picture_pixel(&before, 6, 1) == 0x7f);
	list.texts[1].before = 0;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.texts[1].before = list.count;
	list.texts[1].offset = MH_UI_TEXT_BYTES;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	list.texts[1].offset = 2;
	list.texts[1].font = NULL;
	CHECK(!mh_renderer_scene_frame_ui(
		renderer, NULL, NULL, NULL, 0, &list));
	CHECK(check_retained_frame(renderer, &before) == 0);
	free(before.data);
	mh_ui_begin(&list, 320, 200);
	CHECK(list.text_count == 0 && list.text_size == 0);
	text[0] = 'A';

	for (i = 0; i < MH_UI_MAX_TEXTS; i++)
		CHECK(mh_ui_word_text(&list, &font, text, 1, 0, 0, 0,
			MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);

	saved_size = list.text_size;
	CHECK(mh_ui_word_text(&list, &font, text, 1, 0, 0, 0, MH_UI_TEXT_LEFT,
		NULL) == MH_ERROR_RANGE);
	CHECK(list.text_count == MH_UI_MAX_TEXTS &&
		list.text_size == saved_size);
	mh_ui_begin(&list, 320, 200);
	list.text_size = MH_UI_TEXT_BYTES;
	CHECK(mh_ui_word_text(&list, &font, text, 1, 0, 0, 0, MH_UI_TEXT_LEFT,
		NULL) == MH_ERROR_RANGE);
	CHECK(list.text_count == 0 && list.text_size == MH_UI_TEXT_BYTES);
	mh_renderer_destroy(renderer);

	return 0;
}

static int
sprite_frame(const struct mh_spr *archive, unsigned int entry,
	unsigned int frame, unsigned int green_bits)
{
	struct mh_image image;
	struct mh_sprite_words literal;
	struct mh_sprite_indexed indexed;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region, background;
	struct mh_renderer *renderer;
	struct mh_file picture;
	struct mh_software_ui raw;
	struct mh_software_surface surface;
	unsigned int x, y, expected, column, covered;
	const float blue[4] = {0, 0, 1, 1};
	const unsigned char *rgb;
	size_t i;

	column = green_bits != 0 &&
		strcmp(archive->entries[entry].name, "meter") == 0;
	memset(&literal, 0, sizeof(literal));
	memset(&indexed, 0, sizeof(indexed));

	if (green_bits) {
		CHECK(mh_sprite_spr_decode_literal(archive, entry, frame,
			column, &literal, NULL) == MH_ERROR_NONE);
		CHECK(mh_sprite_spr_decode_literal_rgba(archive, entry, frame,
			column, green_bits, &image,
			NULL) == MH_ERROR_NONE);
	} else {
		CHECK(mh_sprite_spr_decode_indexed(archive, entry, frame,
			&indexed, NULL) == MH_ERROR_NONE);
		CHECK(mh_sprite_spr_decode_rgba(archive, entry, frame, &image,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(image.width <= 640 && image.height <= 480);
	CHECK(mh_ui_atlas_init(image.width + 2, image.height + 2, &atlas,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_ui_atlas_add(&atlas, &image, &region, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 640, 480,
		0, &renderer, NULL) == MH_ERROR_NONE);
	memset(atlas.image.rgba, 255, 4);
	background.x = background.y = 0;
	background.width = background.height = 1;
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_ui_quad_scaled(&list, &atlas, &background, 0, 0,
		(float)image.width, (float)image.height, blue));
	CHECK(mh_ui_quad(&list, &atlas, &region, 0, 0, white));
	CHECK(mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, &list));
	CHECK(mh_renderer_tga_capture(renderer, &picture));
	memset(&raw, 0, sizeof(raw));
	memset(&surface, 0, sizeof(surface));
	surface.width = 640;
	surface.height = 480;
	surface.pitch = 1280;
	surface.size = surface.pitch * surface.height;
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;
	surface.pixels = calloc(1, surface.size);
	CHECK(surface.pixels != NULL);
	CHECK(mh_software_ui_upload(&raw, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(mh_software_ui_check(&raw, &surface, &list, NULL) ==
		MH_ERROR_NONE);
	mh_software_ui_draw(&raw, &surface, &list);

	for (y = 0; y < image.height; y++)
		for (x = 0; x < image.width; x++) {
			i = (size_t)y * image.width + x;

			if (green_bits) {
				expected =
					mh_read_u16le(literal.pixels + i * 3);
				covered = literal.pixels[i * 3 + 2];

				if (green_bits == 5)
					expected = ((expected & 0x7fe0) << 1) |
						((expected >> 4) & 0x20) |
						(expected & 31);
			} else {
				rgb = archive->data + archive->palette_offset +
					indexed.pixels[i * 2] * 3;
				expected = ((unsigned int)(rgb[0] >> 3) << 11) |
					((unsigned int)(rgb[1] >> 2) << 5) |
					(rgb[2] >> 3);
				covered = indexed.pixels[i * 2 + 1];
			}

			CHECK(mh_read_u16le(surface.pixels + y * surface.pitch +
				x * 2) == (covered ? expected : 0x1fU));
			expected = ((expected >> 1) & 0x7fe0) | (expected & 31);
			CHECK(picture_pixel(&picture, x, y) ==
				(covered ? expected : 0x1fU));
		}

	mh_software_ui_free(&raw);
	free(surface.pixels);
	free(picture.data);
	mh_renderer_destroy(renderer);
	free(image.rgba);
	free(atlas.image.rgba);
	free(literal.pixels);
	free(indexed.pixels);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_spr archive;
	unsigned int entry, frame, green_bits, frames;

	if (argc == 1) {
		CHECK(fixtures() == 0);
		CHECK(scene_overlay() == 0);
		CHECK(word_text() == 0);
		puts("Software UI composition, coverage, clipping and atomicity passed");
		return 0;
	}

	if (argc != 3 ||
		(strcmp(argv[2], "0") != 0 && strcmp(argv[2], "5") != 0 &&
			strcmp(argv[2], "6") != 0))
		return 2;

	green_bits = (unsigned int)(argv[2][0] - '0');
	CHECK(mh_file_read(argv[1], &file, NULL) == MH_ERROR_NONE);
	CHECK(mh_sprite_spr_read(file.data, file.size, &archive, NULL) ==
		MH_ERROR_NONE);
	frames = 0;

	for (entry = 0; entry < archive.count; entry++)
		for (frame = 0; frame < archive.entries[entry].frames;
			frame++) {
			CHECK(sprite_frame(
				&archive, entry, frame, green_bits) == 0);
			frames++;
		}

	printf("%u frames match decoded source colors and coverage\n", frames);
	mh_sprite_spr_free(&archive);
	free(file.data);

	return 0;
}
