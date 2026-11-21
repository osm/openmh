#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/core/binary.h"
#include "engine/image/load.h"
#include "engine/image/track_map.h"
#include "engine/ui/hud_assets.h"
#include "engine/ui/loading_assets.h"
#include "interfaces/font/source_backend.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "image decoder:%d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	rgba, indexed;
	int		fail, invalid;
};

static size_t
make_ilbm(unsigned char data[64])
{
	memset(data, 0, 64);
	memcpy(data, "FORM", 4);
	data[7] = 56;
	memcpy(data + 8, "ILBMBMHD", 8);
	data[19] = 20;
	data[21] = 2;
	data[23] = data[28] = 1;
	data[29] = 2;
	data[33] = 1;
	memcpy(data + 40, "CMAP", 4);
	data[47] = 6;
	data[48] = data[51] = 100;
	data[49] = data[52] = 20;
	data[50] = data[53] = 30;
	memcpy(data + 54, "BODY", 4);
	data[61] = 2;
	data[62] = 0x40;

	return 64;
}

static size_t
make_tga(unsigned char data[64])
{
	memset(data, 0, 64);
	data[1] = data[2] = 1;
	data[5] = data[12] = 2;
	data[7] = 32;
	data[14] = 1;
	data[16] = 8;
	data[17] = 32;
	data[18] = data[22] = 30;
	data[19] = data[23] = 20;
	data[20] = data[24] = 100;
	data[21] = 44;
	data[25] = 80;
	data[27] = 1;

	return 28;
}

static int
check_original(int ilbm)
{
	const struct mh_image_decoder_provider *decoder;
	struct mh_image_decode_options options;
	struct mh_image image, before;
	struct mh_image_palette palette, replacement, saved;
	struct mh_indexed_image indexed;
	unsigned char data[64];
	const char *extension;
	size_t size;

	decoder = mh_image_fallback_decoder();
	CHECK(mh_image_decoder_extension(decoder, &extension, NULL) ==
		MH_ERROR_NONE);
	CHECK(extension == NULL);
	size = ilbm ? make_ilbm(data) : make_tga(data);
	CHECK(mh_image_decode(decoder, data, size, NULL, &image, &palette,
		NULL) == MH_ERROR_NONE);
	CHECK(image.width == 2 && image.height == 1);
	CHECK(palette.count == 2 &&
		memcmp(palette.rgb, palette.rgb + 3, 3) == 0);
	CHECK(image.rgba[0] == 100 && image.rgba[1] == 20 &&
		image.rgba[2] == 30);
	CHECK(image.rgba[3] == (ilbm ? 255 : 44));
	CHECK(image.rgba[7] == (ilbm ? 0 : 80));
	free(image.rgba);
	CHECK(mh_image_decode_indexed(decoder, data, size, &indexed, NULL) ==
		MH_ERROR_NONE);
	CHECK(indexed.size == 2 && indexed.colors == 2);
	CHECK(indexed.indices[0] == 0 && indexed.indices[1] == 1);
	CHECK(memcmp(indexed.palette, indexed.palette + 3, 3) == 0);
	free(indexed.indices);
	memset(&replacement, 0, sizeof(replacement));
	replacement.count = 2;
	replacement.rgb[0] = replacement.rgb[2] = replacement.rgb[5] = 255;
	options.palette = &replacement;
	options.alpha = MH_IMAGE_ALPHA_INDEX_ZERO;
	CHECK(mh_image_decode(decoder, data, size, &options, &image, &palette,
		NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[0] == 255 && image.rgba[2] == 255 &&
		image.rgba[3] == 0);
	CHECK(image.rgba[4] == 0 && image.rgba[6] == 255 &&
		image.rgba[7] == 255);
	CHECK(memcmp(palette.rgb, replacement.rgb, sizeof(palette.rgb)) == 0);
	free(image.rgba);
	options.alpha = MH_IMAGE_ALPHA_MAGENTA;
	CHECK(mh_image_decode(decoder, data, size, &options, &image, NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[3] == 0 && image.rgba[7] == (ilbm ? 0 : 80));
	before = image;
	saved = palette;
	CHECK(mh_image_decode(decoder, data, size - 1, NULL, &image, &palette,
		NULL) != MH_ERROR_NONE);
	CHECK(image.rgba == before.rgba && image.width == before.width);
	CHECK(memcmp(&palette, &saved, sizeof(palette)) == 0);
	free(image.rgba);

	return 0;
}

static enum mh_error_code
raw_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_image image;

	fixture = context;
	fixture->rgba++;

	if (size != 1 ||
		(data[0] != 'B' && data[0] != 'F' && data[0] != 'S' &&
			data[0] != 'M' && data[0] != 'X') ||
		options->alpha !=
			(data[0] == 'M' ? MH_IMAGE_ALPHA_INDEX_ZERO
					: MH_IMAGE_ALPHA_FILE))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test image request");

	if (data[0] == 'B' && options->palette != NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unexpected test palette");

	if (data[0] == 'F' &&
		(options->palette == NULL || options->palette->count != 256 ||
			options->palette->rgb[0] != 0 ||
			options->palette->rgb[765] != 255))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test font palette");

	if (data[0] == 'X')
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "test decoder failure");

	image.width = data[0] == 'F' ? 16 : data[0] == 'B' ? 640 : 2;
	image.height = data[0] == 'F' ? 16 : data[0] == 'B' ? 400 : 1;
	image.rgba = malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY, "test pixels");

	memset(image.rgba, data[0] == 'F' ? 255 : 123,
		(size_t)image.width * image.height * 4);

	if (data[0] == 'S' || data[0] == 'M') {
		image.rgba[0] = image.rgba[2] = image.rgba[3] = 255;
		image.rgba[1] = 0;

		if (data[0] == 'M')
			image.rgba[3] = 0;

		image.rgba[7] = 255;
	}

	if (fixture->invalid)
		image.width = 0;

	*output = image;

	if (palette != NULL)
		memset(palette, 0, sizeof(*palette));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
raw_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_indexed_image image;

	fixture = context;
	fixture->indexed++;

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test indexed read failure");

	if (size != 1 || data[0] != 'I')
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test indexed request");

	memset(&image, 0, sizeof(image));
	image.width = 12;
	image.height = 33;
	image.size = 12 * 33;
	image.colors = 2;
	image.indices = calloc(image.size, 1);

	if (image.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY, "test indices");

	image.indices[0] = 1;
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
alternate_glyphs(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_font font;
	unsigned int i;

	(void)context;
	(void)resources;

	if (strcmp(root, "root") != 0 ||
		(strcmp(reference, "Data/font1s.fnt") != 0 &&
			strcmp(reference, "Data/font2.fnt") != 0))
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown test glyphs");

	memset(&font, 0, sizeof(font));
	font.image.width = font.image.height = 32;
	font.cell_width = font.height = 2;
	font.image.rgba = malloc(32 * 32 * 4);

	if (font.image.rgba == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test glyph allocation");

	memset(font.image.rgba, 255, 32 * 32 * 4);

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 2;

	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
alternate_font(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_font_program *output,
	struct mh_error *error_out)
{
	struct mh_font_program font;
	unsigned int i;

	(void)context;
	(void)resources;

	if (strcmp(root, "root") != 0 ||
		strcmp(reference, "Data/font2.fnt") != 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "unknown test font");

	memset(&font, 0, sizeof(font));
	font.width = font.rows = 16;
	font.cell_width = font.height = 1;
	font.bytes = calloc(256, 1);

	if (font.bytes == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test font allocation");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 1;

	font.bytes['A'] = MH_FONT_LOW;
	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_alternate(void)
{
	static const struct mh_font_source_ops font_ops = {
		NULL, alternate_font};

	static const struct mh_font_source_provider fonts = {&font_ops, NULL};
	static const struct mh_image_decoder_ops ops = {raw_rgba, raw_indexed};
	struct mh_image_decoder_provider decoder;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[3];
	struct mh_loading_assets assets, before;
	struct mh_image image, saved;
	struct fixture fixture;
	unsigned int frame;
	size_t pixel;
	char path[128];

	memset(&fixture, 0, sizeof(fixture));
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	entries[0].path = "root/Data/Loading.raw";
	entries[0].data = (const unsigned char *)"B";
	entries[0].size = 1;
	entries[1].path = "root/Data/LoadAnim.raw";
	entries[1].data = (const unsigned char *)"I";
	entries[1].size = 1;
	entries[2].path = "root/Data/d3d/myfont.raw";
	entries[2].data = (const unsigned char *)"F";
	entries[2].size = 1;
	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_image_path_from(&decoder, resources, "root",
		"data/loading.TGA", path, sizeof(path),
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, entries[0].path) == 0);
	CHECK(mh_loading_assets_load_from(&fonts, &decoder, resources, "root",
		MH_RENDER_SOFTWARE, 640, 400, &assets,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.rgba == 1 && fixture.indexed == 1);
	CHECK(assets.word_font.bytes['A'] == MH_FONT_LOW &&
		assets.word_font.advance['A'] == 1);

	for (frame = 0; frame < 3; frame++) {
		pixel = ((size_t)(assets.indicator.y +
				frame * assets.indicator.height) *
					assets.atlas.image.width +
				assets.indicator.x) *
			4;
		CHECK(assets.atlas.image.rgba[pixel] == 0);
		CHECK(assets.atlas.image.rgba[pixel + 3] == 255);
		CHECK(assets.atlas.image.rgba[pixel + 7] == 0);
	}

	before = assets;
	fixture.fail = 1;
	CHECK(mh_loading_assets_load_from(&fonts, &decoder, resources, "root",
		MH_RENDER_SOFTWARE, 640, 400, &assets,
		NULL) == MH_ERROR_IO);
	CHECK(memcmp(&assets, &before, sizeof(assets)) == 0);
	mh_loading_assets_free(&assets);
	fixture.fail = 0;
	CHECK(mh_loading_assets_load_from(&fonts, &decoder, resources, "root",
		MH_RENDER_GLIDE, 640, 400, &assets,
		NULL) == MH_ERROR_NONE);
	CHECK(assets.advance['A'] == 2 && assets.advance[' '] == 1);
	CHECK(assets.glyphs['A'].width == 1 && assets.glyphs['A'].height == 1);
	mh_loading_assets_free(&assets);
	memset(&saved, 0, sizeof(saved));
	saved.width = 99;
	image = saved;
	fixture.invalid = 1;
	CHECK(mh_image_load_from(&decoder, resources, "root",
		"data/Loading.tga", NULL, &image, NULL,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	CHECK(mh_image_load_from(mh_image_fallback_decoder(), resources, "root",
		"data/Loading.tga", NULL, &image, NULL,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	mh_memory_resources_close(resources);

	return 0;
}

static int
check_hud_pixels(const struct mh_hud_assets *hud)
{
	const struct mh_ui_region *region;
	const unsigned char *pixels;
	unsigned int i;
	size_t offset;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		region = &hud->sprites[i];

		if (i == MH_HUD_MPH && region->width == 0)
			continue;

		CHECK(region->width == 2 && region->height == 1);
		offset = ((size_t)region->y * hud->atlas.image.width +
				region->x) *
			4;
		pixels = hud->atlas.image.rgba + offset;
		CHECK(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 255);
		CHECK(pixels[3] == 0 && pixels[4] == 123 && pixels[7] == 255);
	}

	CHECK(hud->hardware_advance['A'] == 2);
	CHECK(hud->hardware_glyphs['A'].width == 1);
	CHECK(hud->hardware_glyphs['A'].height == 1);
	CHECK(strcmp(hud->secret_scroll, "SWEMOB\r\n") == 0);

	return 0;
}

static int
check_hud_map(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, struct mh_hud_assets *hud)
{
	struct mh_track track;
	struct mh_image map;
	const unsigned char *pixels;
	size_t offset;

	memset(&track, 0, sizeof(track));
	CHECK(mh_track_map_load_from(decoder, resources, "root", &track, &map,
		NULL) == MH_ERROR_NONE);
	CHECK(map.width == 0 && map.height == 0 && map.rgba == NULL);
	strcpy(track.map_name, "Tracks/Track3/Data/Okkcity");
	CHECK(mh_track_map_load_from(decoder, resources, "root", &track, &map,
		NULL) == MH_ERROR_INVALID);
	CHECK(map.rgba == NULL);
	track.has_map_info = 1;
	CHECK(mh_track_map_load_from(decoder, resources, "root", &track, &map,
		NULL) == MH_ERROR_NONE);
	CHECK(map.width == 2 && map.height == 1);
	CHECK(map.rgba[0] == 0 && map.rgba[3] == 0);
	CHECK(map.rgba[4] == 123 && map.rgba[7] == 255);
	free(map.rgba);
	CHECK(mh_hud_assets_map_from(decoder, resources, "root", &track, hud,
		NULL) == MH_ERROR_NONE);
	CHECK(hud->map.width == 2 && hud->map.height == 1);
	CHECK(hud->map_offset[0] == -12 && hud->map_offset[1] == -2);
	offset = ((size_t)hud->map.y * hud->atlas.image.width + hud->map.x) * 4;
	pixels = hud->atlas.image.rgba + offset;
	CHECK(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0 &&
		pixels[3] == 0);
	CHECK(pixels[4] == 123 && pixels[7] == 255);

	return 0;
}

static int
check_alternate_hud(void)
{
	static const struct mh_font_source_ops font_ops = {
		alternate_glyphs, NULL};

	static const struct mh_font_source_provider fonts = {&font_ops, NULL};
	static const struct mh_image_decoder_ops ops = {raw_rgba, raw_indexed};
	static const char *const names[] = {"tkmh00", "tgear00", "r00",
		"meter00", "kolon00", "tof00", "total", "time", "lap", "best",
		"pos", "count00", "count01", "count02", "count03", "tww00",
		"plupp", "pluppother", "avenger", "mapnr1", "mapnr2", "mapnr3",
		"mapnr4", "mapnr5", "mapnr6", "mapnr7", "mapnr8", "race",
		"last", "ever", "cam00", "ghost00", "tkmh01"};

	struct mh_image_decoder_provider decoder;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[MH_HUD_SPRITES + 3];
	struct mh_hud_assets hud, before;
	struct fixture fixture;
	char paths[MH_HUD_SPRITES][80];
	unsigned int i, count, optional;

	memset(&fixture, 0, sizeof(fixture));
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	entries[0].path = "root/Data/d3d/myfont.raw";
	entries[0].data = (const unsigned char *)"F";
	entries[0].size = 1;
	entries[1].path = "root/Data/d3d/tullinge.iff";
	entries[1].data = (const unsigned char *)"SWEMOB\r\n";
	entries[1].size = 8;
	entries[2].path = "root/Tracks/Track3/Data/Okkcity.raw";
	entries[2].data = (const unsigned char *)"M";
	entries[2].size = 1;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		if (i < MH_HUD_KMH)
			sprintf(paths[i], "root/Data/d3d/%cnum%u00.raw",
				i < MH_HUD_BIG ? 's' : 'b', i % 10);
		else
			sprintf(paths[i], "root/Data/d3d/%s.raw",
				names[i - MH_HUD_KMH]);
		entries[i + 3].path = paths[i];
		entries[i + 3].data = (const unsigned char *)"S";
		entries[i + 3].size = 1;
	}

	count = MH_HUD_SPRITES + 3;
	CHECK(MH_HUD_MPH == MH_HUD_SPRITES - 1);

	for (optional = 0; optional < 2; optional++) {
		CHECK(mh_memory_resources_open(entries, count - optional,
			&resources, NULL) == MH_ERROR_NONE);
		CHECK(mh_hud_assets_load_from(&fonts, &decoder, resources,
			"root", &hud, NULL) == MH_ERROR_NONE);
		CHECK(hud.sprites[MH_HUD_MPH].width == (optional ? 0 : 2));
		CHECK(hud.message_advance['A'] == 2 && hud.advance['A'] == 2);
		CHECK(check_hud_pixels(&hud) == 0);
		CHECK(check_hud_map(&decoder, resources, &hud) == 0);
		free(hud.atlas.image.rgba);
		mh_memory_resources_close(resources);
	}

	/* A present optional image which fails decoding must not be skipped. */
	entries[count - 1].data = (const unsigned char *)"X";
	CHECK(mh_memory_resources_open(entries, count, &resources, NULL) ==
		MH_ERROR_NONE);
	memset(&before, 0, sizeof(before));
	before.hardware_height = 99;
	hud = before;
	CHECK(mh_hud_assets_load_from(&fonts, &decoder, resources, "root", &hud,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&hud, &before, sizeof(hud)) == 0);
	mh_memory_resources_close(resources);

	return 0;
}

static int
check_effect_fallback(void)
{
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *resources;
	struct mh_indexed_image image;
	unsigned char source[64], hardware[64];
	unsigned int x, y;

	entries[0].path = "root/Data/smoke/smoke21.iff";
	entries[0].data = source;
	entries[0].size = make_ilbm(source);
	entries[1].path = "root/Data/d3d/smoke/smoke01.iff";
	entries[1].data = hardware;
	entries[1].size = make_tga(hardware);
	CHECK(mh_memory_resources_open(entries, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_image_load_indexed_from(mh_image_fallback_decoder(), resources,
		"root", "Data/d3d/smoke/smoke01.iff", &image,
		NULL) == MH_ERROR_NONE);
	CHECK(image.width == 32 && image.height == 32);
	CHECK(image.palette[0] == 100 && image.palette[1] == 20 &&
		image.palette[2] == 30);

	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++)
			CHECK(image.indices[y * 32 + x] == (x < 16 ? 0 : 1));

	free(image.indices);
	CHECK(mh_image_load_indexed_from(mh_image_fallback_decoder(), resources,
		"root", "Data/d3d/smoke/smoke02.iff", &image,
		NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(resources);

	/*
	 * Installed hardware artwork takes precedence; corruption is not
	 * hidden.
	 */
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_image_load_indexed_from(mh_image_fallback_decoder(), resources,
		"root", "Data/d3d/smoke/smoke01.iff", &image,
		NULL) == MH_ERROR_NONE);
	CHECK(image.width == 2 && image.height == 1);
	free(image.indices);
	hardware[2] = 255;
	CHECK(mh_image_load_indexed_from(mh_image_fallback_decoder(), resources,
		"root", "Data/d3d/smoke/smoke01.iff", &image,
		NULL) != MH_ERROR_NONE);
	mh_memory_resources_close(resources);

	return 0;
}

int
main(void)
{
	CHECK(check_effect_fallback() == 0);
	CHECK(check_original(0) == 0);
	CHECK(check_original(1) == 0);
	CHECK(check_alternate() == 0);
	CHECK(check_alternate_hud() == 0);
	puts("Selected image decoding preserves palettes, alpha and converted loading/HUD artwork");

	return 0;
}
