#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/image/portrait.h"
#include "engine/ui/results_assets.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "results image:%d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	BACKGROUND_WIDTH = 640,
	BACKGROUND_HEIGHT = 400
};

struct fixture {
	unsigned int	calls;
	int		bad_size, bad_color, fail_portrait;
};

static enum mh_error_code
raw_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_image image;
	size_t bytes, i;
	int portrait;

	fixture = context;
	fixture->calls++;

	if (size != 1 || options->alpha != MH_IMAGE_ALPHA_FILE ||
		options->palette != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected test background request");

	portrait = data[0] == 'P';

	if (portrait && fixture->fail_portrait)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"test malformed portrait");

	image.width = portrait	    ? MH_PORTRAIT_WIDTH
		: fixture->bad_size ? 1
				: BACKGROUND_WIDTH;
	image.height = portrait	    ? MH_PORTRAIT_HEIGHT
		: fixture->bad_size ? 1
				: BACKGROUND_HEIGHT;
	bytes = (size_t)image.width * image.height * 4;
	image.rgba = malloc(bytes);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY, "test pixels");

	memset(image.rgba, data[0], bytes);

	if (portrait) {
		for (i = 0; i < MH_PORTRAIT_PIXELS; i++) {
			memset(image.rgba + i * 4, (unsigned char)(i * 17 + 3),
				3);
			image.rgba[i * 4 + 3] = 0;
		}

		if (fixture->bad_color)
			image.rgba[bytes - 2] ^= 1;
	}

	*output = image;

	if (palette != NULL)
		memset(palette, 0, sizeof(*palette));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
raw_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	(void)context;

	(void)data;
	(void)size;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"test backgrounds have no indexed view");
}

static enum mh_error_code
load_glyphs(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_font font;
	unsigned int i;

	(void)context;
	(void)resources;

	if (strcmp(root, "root") != 0 ||
		strcmp(reference, "Data/font1.fnt") != 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown results font");

	memset(&font, 0, sizeof(font));
	font.image.width = font.image.height = 32;
	font.cell_width = font.height = 2;
	font.image.rgba = malloc(32 * 32 * 4);

	if (font.image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"test results glyph allocation");

	memset(font.image.rgba, 255, 32 * 32 * 4);

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 2;

	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_board(const struct mh_image_decoder_provider *decoder,
	const struct mh_image_decoder_provider *portraits,
	const struct mh_resource_source *resources, unsigned int board,
	struct mh_ui_atlas *atlas, struct mh_results_assets *output)
{
	static const struct mh_font_source_ops font_ops = {load_glyphs, NULL};
	static const struct mh_font_source_provider fonts = {&font_ops, NULL};

	const struct mh_profile *profiles[8];
	const char *names[8];
	struct mh_profile profile;
	unsigned int i, count;

	memset(&profile, 0, sizeof(profile));
	strcpy(profile.name, "Driver");
	strcpy(profile.nick, "Driver");
	strcpy(profile.picture, "absent.tga");
	count = board % 2 ? 8 : 2;

	for (i = 0; i < count; i++) {
		profiles[i] = &profile;
		names[i] = "Driver";
	}

	if (board < 2)
		return mh_results_assets_players_from(&fonts, decoder,
			portraits, resources, "root", count, names, atlas,
			output, NULL);

	return mh_results_assets_season_from(&fonts, decoder, portraits,
		resources, "root", count, profiles, 1, atlas, output, NULL);
}

static int
check_pixels(const struct mh_ui_atlas *atlas,
	const struct mh_results_assets *results, unsigned char value,
	int present)
{
	const struct mh_ui_region *region;
	const unsigned char *pixels;
	unsigned int x, y, i, gray;
	size_t offset;

	region = &results->background;
	CHECK(region->width == BACKGROUND_WIDTH &&
		region->height == BACKGROUND_HEIGHT);

	for (y = 0; y < region->height; y++) {
		offset = ((size_t)(region->y + y) * atlas->image.width +
				region->x) *
			4;
		pixels = atlas->image.rgba + offset;

		for (x = 0; x < region->width * 4; x++)
			CHECK(pixels[x] == value);
	}

	for (i = 0; i < results->count; i++) {
		region = &results->portraits[i];
		CHECK(region->width == MH_PORTRAIT_WIDTH &&
			region->height == MH_PORTRAIT_HEIGHT);

		for (y = 0; y < region->height; y++) {
			offset = ((size_t)(region->y + y) * atlas->image.width +
					region->x) *
				4;
			pixels = atlas->image.rgba + offset;

			for (x = 0; x < region->width; x++) {
				gray = present
					? (((y * MH_PORTRAIT_WIDTH + x) * 17 +
						3) &
						248U)
					: 0;

				CHECK(pixels[x * 4] == gray &&
					pixels[x * 4 + 1] == gray &&
					pixels[x * 4 + 2] == gray &&
					pixels[x * 4 + 3] == 255);
			}
		}
	}

	return 0;
}

static int
check_board(unsigned int board, int present)
{
	static const struct mh_image_decoder_ops ops = {raw_rgba, raw_indexed};
	static const char *const paths[] = {"root/Data/results2.raw",
		"root/Data/results.raw", "root/Data/lresults2.raw",
		"root/Data/lresults.raw"};

	struct mh_image_decoder_provider decoder, portraits;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[3];
	struct mh_results_assets results, before;
	struct mh_ui_atlas atlas;
	struct fixture fixture;
	unsigned char value;

	memset(&fixture, 0, sizeof(fixture));
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	portraits = decoder;
	portraits.extension = "gray";
	value = (unsigned char)(100 + board);
	entries[0].path = paths[board];
	entries[0].data = &value;
	entries[0].size = 1;
	entries[1].path = "root/Game/player.gray";
	entries[1].data = (const unsigned char *)"P";
	entries[1].size = 1;
	entries[2] = entries[1];
	entries[2].path = "root/League/Profiles/Gfx/absent.gray";
	CHECK(mh_memory_resources_open(entries, present ? 3 : 1, &resources,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_ui_atlas_init(1024, 1024, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(load_board(&decoder, &portraits, resources, board, &atlas,
		&results) == MH_ERROR_NONE);
	CHECK(fixture.calls ==
		(present ? (board < 2 ? 2U : 1 + results.count) : 1U));
	CHECK(results.advance['A'] == 2 && results.glyphs['A'].height == 2);
	CHECK(check_pixels(&atlas, &results, value, present) == 0);
	free(atlas.image.rgba);

	/*
	 * Authored background dimensions remain mandatory with another codec.
	 */
	CHECK(mh_ui_atlas_init(1024, 1024, &atlas, NULL) == MH_ERROR_NONE);
	before = results;
	fixture.bad_size = 1;
	CHECK(load_board(&decoder, &portraits, resources, board, &atlas,
		&results) == MH_ERROR_INVALID);
	CHECK(memcmp(&results, &before, sizeof(results)) == 0);
	free(atlas.image.rgba);

	if (present) {
		fixture.bad_size = 0;
		fixture.bad_color = 1;
		CHECK(mh_ui_atlas_init(1024, 1024, &atlas, NULL) ==
			MH_ERROR_NONE);
		CHECK(load_board(&decoder, &portraits, resources, board, &atlas,
			&results) == MH_ERROR_INVALID);
		CHECK(memcmp(&results, &before, sizeof(results)) == 0);
		free(atlas.image.rgba);

		/*
		 * A decode error for a present portrait is not a missing-file
		 * fallback.
		 */
		fixture.bad_color = 0;
		fixture.fail_portrait = 1;
		CHECK(mh_ui_atlas_init(1024, 1024, &atlas, NULL) ==
			MH_ERROR_NONE);
		CHECK(load_board(&decoder, &portraits, resources, board, &atlas,
			&results) == MH_ERROR_NOT_FOUND);
		CHECK(memcmp(&results, &before, sizeof(results)) == 0);
		free(atlas.image.rgba);
	}

	mh_memory_resources_close(resources);

	return 0;
}

int
main(void)
{
	unsigned int board;

	for (board = 0; board < 4; board++) {
		CHECK(check_board(board, 0) == 0);
		CHECK(check_board(board, 1) == 0);
	}

	puts("Race and season results use selected background/portrait decoding and preserve grayscale fallback");

	return 0;
}
