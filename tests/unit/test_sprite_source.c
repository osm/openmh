#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/ui/software_hud_assets.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/sprite/collection_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "sprite source:%d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	bank, opens, closes, frames, fonts;
	int		missing_mph, fail;
};

struct collection {
	struct fixture	*fixture;
	unsigned char	color;
};

static enum mh_error_code
open_collection(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_picture_format format,
	void **output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct collection *collection;
	struct mh_file file;
	char expected[64];
	enum mh_error_code status;

	fixture = context;
	sprintf(expected, "Data/game%u.spr", fixture->bank);

	if (strcmp(reference, expected) != 0) {
		sprintf(expected, "Tracks/map%u.dta", fixture->bank);

		if (strcmp(reference, expected) != 0)
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"unknown fixture collection");
	}

	if (strcmp(root, "converted") != 0 ||
		format !=
			(fixture->bank % 2 ? MH_PICTURE_RGB565
					: MH_PICTURE_RGB555))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"wrong fixture color model");

	/*
	 * A replacement collection maps original references to unrelated bytes.
	 */
	status = mh_resource_source_read(
		resources, "converted/art/frames.bin", &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.size != 1) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid fixture collection");
	}

	collection = malloc(sizeof(*collection));

	if (collection == NULL) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"fixture collection allocation");
	}

	collection->fixture = fixture;
	collection->color = file.data[0];
	free(file.data);
	fixture->opens++;
	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_info(void *context, void *state, const char *name,
	struct mh_sprite_info *output, struct mh_error *error_out)
{
	const struct collection *collection;
	struct mh_sprite_info info;

	(void)context;
	collection = state;

	info.frames = strcmp(name, "count") == 0 ? 4 : 2;

	if (strcmp(name, "tkmh") == 0 && collection->fixture->missing_mph)
		info.frames = 1;

	info.width = info.height = 2;
	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_rgba(void *context, void *state, const char *name, unsigned int frame,
	struct mh_image *output, struct mh_error *error_out)
{
	struct collection *collection;
	struct mh_image image;
	unsigned int i;

	(void)context;
	collection = state;

	if (collection->fixture->fail && strcmp(name, "count") == 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "fixture frame read failed");

	image.width = image.height = 2;
	image.rgba = malloc(16);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"fixture sprite allocation");

	for (i = 0; i < 4; i++) {
		image.rgba[i * 4] = collection->color;
		image.rgba[i * 4 + 1] = (unsigned char)frame;
		image.rgba[i * 4 + 2] = (unsigned char)name[0];
		image.rgba[i * 4 + 3] = i == 0 ? 0 : 255;
	}

	collection->fixture->frames++;
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_collection(void *context, void *state)
{
	struct collection *collection;

	(void)context;
	collection = state;
	collection->fixture->closes++;
	free(collection);
}

static enum mh_error_code
load_font(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font_program *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_font_program font;
	unsigned int i;

	(void)resources;
	fixture = context;

	if (strcmp(root, "converted") != 0 ||
		strcmp(reference,
			fixture->bank >= 2 ? "Data/font2s.fnt"
					: "Data/font2.fnt") != 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown fixture font");

	memset(&font, 0, sizeof(font));
	font.width = font.rows = 16;
	font.cell_width = font.height = 1;
	font.bytes = calloc(256, 1);

	if (font.bytes == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "fixture font allocation");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 1;

	font.bytes['A'] = MH_FONT_LOW;
	fixture->fonts++;
	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_region(const struct mh_software_hud_assets *assets,
	const struct mh_ui_region *region, unsigned int frame,
	unsigned char name)
{
	const unsigned char *pixels;
	size_t offset;

	CHECK(region->width == 2 && region->height == 2);
	offset =
		((size_t)region->y * assets->atlas.image.width + region->x) * 4;
	pixels = assets->atlas.image.rgba + offset;
	CHECK(pixels[0] == 91 && pixels[1] == frame && pixels[2] == name);
	CHECK(pixels[3] == 0 && pixels[4] == 91 && pixels[7] == 255);

	return 0;
}

static int
check_hud(unsigned int bank)
{
	static const struct mh_sprite_source_ops sprite_ops = {open_collection,
		sprite_info, NULL, NULL, sprite_rgba, close_collection};

	static const struct mh_font_source_ops font_ops = {NULL, load_font};
	static const unsigned char color = 91;
	struct mh_sprite_source_provider sprites;
	struct mh_font_source_provider fonts;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entry;
	struct mh_software_hud_assets assets, before;
	struct mh_track track;
	struct fixture fixture;
	unsigned int width, green_bits;

	memset(&fixture, 0, sizeof(fixture));
	fixture.bank = bank;
	sprites.ops = &sprite_ops;
	sprites.context = &fixture;
	fonts.ops = &font_ops;
	fonts.context = &fixture;
	entry.path = "converted/art/frames.bin";
	entry.data = &color;
	entry.size = 1;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	memset(&track, 0, sizeof(track));
	strcpy(track.map_name, "Tracks/map");
	track.has_map_info = 1;
	track.map_info[0] = 123;
	width = bank >= 2 ? 320 : 640;
	green_bits = 5 + bank % 2;
	CHECK(mh_software_hud_assets_load_from(&sprites, &fonts, resources,
		"converted", &track, width, green_bits, &assets,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.opens == 2 && fixture.closes == 2 && fixture.fonts == 1);
	CHECK(fixture.frames ==
		MH_SOFTWARE_HUD_SPRITES + 1 + 2 * MH_SOFTWARE_HUD_RANKS);
	CHECK(assets.font.bytes['A'] == MH_FONT_LOW &&
		assets.map_info[0] == 123);
	CHECK(check_region(&assets,
		&assets.sprites[MH_SOFTWARE_HUD_METER_EMPTY], 1,
		'm') == 0);
	CHECK(check_region(&assets, &assets.sprites[MH_SOFTWARE_HUD_GO], 3,
		'c') == 0);
	CHECK(check_region(&assets, &assets.map, 0, 'm') == 0);
	CHECK(check_region(&assets, &assets.small_markers[7], 0,
		bank >= 2 ? 'l' : 'h') == 0);
	before = assets;
	fixture.fail = 1;
	CHECK(mh_software_hud_assets_load_from(&sprites, &fonts, resources,
		"converted", &track, width, green_bits, &assets,
		NULL) == MH_ERROR_IO);
	CHECK(memcmp(&assets, &before, sizeof(assets)) == 0);
	CHECK(fixture.opens == 3 && fixture.closes == 3);
	mh_software_hud_assets_free(&assets);
	fixture.fail = 0;
	fixture.missing_mph = 1;
	CHECK(mh_software_hud_assets_load_from(&sprites, &fonts, resources,
		"converted", NULL, width, green_bits, &assets,
		NULL) == MH_ERROR_NONE);
	CHECK(assets.sprites[MH_SOFTWARE_HUD_MPH].width == 0);
	CHECK(fixture.opens == 4 && fixture.closes == 4);
	mh_memory_resources_close(resources);
	CHECK(check_region(&assets, &assets.sprites[MH_SOFTWARE_HUD_GO], 3,
		'c') == 0);
	mh_software_hud_assets_free(&assets);

	return 0;
}

int
main(void)
{
	unsigned int bank;

	for (bank = 0; bank < 4; bank++)
		CHECK(check_hud(bank) == 0);

	puts("All software HUD banks/maps use selected sprite/font sources without SPR/FNT files");

	return 0;
}
