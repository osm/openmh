#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_fonts.h"
#include "engine/ui/hud_assets.h"
#include "engine/ui/software_hud_assets.h"
#include "interfaces/font/source.h"

struct hud_label {
	unsigned int	id;
	const char	*text;
};

/*
 * Software editions ship SPR instruments instead of the separate GPU HUD art.
 */
static void
copy_instruments(
	const struct mh_software_hud_assets *source, struct mh_hud_assets *hud)
{
	static const unsigned int instruments[][2] = {
		{MH_HUD_KMH, MH_SOFTWARE_HUD_KMH},
		{MH_HUD_GEAR, MH_SOFTWARE_HUD_GEAR},
		{MH_HUD_REVERSE, MH_SOFTWARE_HUD_REVERSE},
		{MH_HUD_METER, MH_SOFTWARE_HUD_METER},
		{MH_HUD_COLON, MH_SOFTWARE_HUD_COLON},
		{MH_HUD_SLASH, MH_SOFTWARE_HUD_SLASH},
		{MH_HUD_THREE, MH_SOFTWARE_HUD_THREE},
		{MH_HUD_TWO, MH_SOFTWARE_HUD_TWO},
		{MH_HUD_ONE, MH_SOFTWARE_HUD_ONE},
		{MH_HUD_GO, MH_SOFTWARE_HUD_GO},
		{MH_HUD_WRONG_WAY, MH_SOFTWARE_HUD_WRONG_WAY},
		{MH_HUD_CAMERA, MH_SOFTWARE_HUD_CAMERA},
		{MH_HUD_GHOST, MH_SOFTWARE_HUD_GHOST},
		{MH_HUD_MPH, MH_SOFTWARE_HUD_MPH}};

	unsigned int i;

	for (i = 0; i < MH_HUD_KMH; i++)
		hud->sprites[i] = source->sprites[i];

	for (i = 0; i < sizeof(instruments) / sizeof(instruments[0]); i++)
		hud->sprites[instruments[i][0]] =
			source->sprites[instruments[i][1]];

	hud->map = source->map;
	memcpy(hud->map_info, source->map_info, sizeof(hud->map_info));
	hud->solid = source->solid;
}

/* The GPU HUD projects maps at half the software 640-pixel artwork size. */
static enum mh_error_code
add_map_region(const struct mh_software_hud_assets *source,
	const struct mh_ui_region *region, unsigned int top,
	unsigned int height, struct mh_hud_assets *hud,
	struct mh_ui_region *output, struct mh_error *error_out)
{
	struct mh_image image;
	const unsigned char *row;
	unsigned char *target;
	unsigned int x, y;
	enum mh_error_code status;

	if (region->width == 0 || region->height == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (top >= region->height || height > region->height - top)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software map artwork");

	image.width = (region->width + 1) / 2;
	image.height = (height + 1) / 2;
	image.rgba = malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory adapting map artwork");

	for (y = 0; y < image.height; y++) {
		row = source->atlas.image.rgba +
			((size_t)(region->y + top + y * 2) *
					source->atlas.image.width +
				region->x) *
				4;
		target = image.rgba + (size_t)y * image.width * 4;

		for (x = 0; x < image.width; x++)
			memcpy(target + x * 4, row + x * 8, 4);
	}

	status = mh_ui_atlas_add(&hud->atlas, &image, output, error_out);
	free(image.rgba);

	return status;
}

static enum mh_error_code
add_map(const struct mh_software_hud_assets *source, struct mh_hud_assets *hud,
	struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	status = add_map_region(source, &source->map, 0, source->map.height,
		hud, &hud->map, error_out);

	/*
	 * Rank images place the label above the marker's outline (software
	 * y+13).
	 */
	if (status == MH_ERROR_NONE && source->map.width != 0)
		status = add_map_region(source, &source->large_markers[0], 13,
			source->large_markers[0].height - 13, hud,
			&hud->sprites[MH_HUD_MARKER], error_out);

	if (status == MH_ERROR_NONE && source->map.width != 0)
		status = add_map_region(source, &source->small_markers[0], 13,
			source->small_markers[0].height - 13, hud,
			&hud->sprites[MH_HUD_OTHER_MARKER], error_out);

	hud->sprites[MH_HUD_AVENGER] = hud->sprites[MH_HUD_MARKER];

	for (i = 0; status == MH_ERROR_NONE && source->map.width != 0 &&
		i < MH_HUD_MAP_RANKS;
		i++)
		status = add_map_region(source, &source->large_markers[i], 0,
			13, hud, &hud->sprites[MH_HUD_MAP_NUMBER + i],
			error_out);

	return status;
}

static enum mh_error_code
add_label(const struct mh_font *font, const char *text,
	struct mh_hud_assets *hud, unsigned int id, struct mh_error *error_out)
{
	struct mh_image image;
	const unsigned char *p;
	unsigned int x, y, width, offset, sx, sy;
	enum mh_error_code status;

	width = 0;

	for (p = (const unsigned char *)text; *p; p++)
		width += font->advance[*p];

	image.width = width;
	image.height = font->height;
	image.rgba = calloc((size_t)width * image.height, 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing HUD label");

	offset = 0;

	for (p = (const unsigned char *)text; *p; p++) {
		sx = *p % 16 * font->cell_width;
		sy = *p / 16 * font->height;

		for (y = 0; y < font->height; y++)
			for (x = 0; x < font->advance[*p]; x++)
				memcpy(image.rgba +
						((size_t)y * width + offset +
							x) *
							4,
					font->image.rgba +
						((size_t)(sy + y) *
								font->image
									.width +
							sx + x) *
							4,
					4);

		offset += font->advance[*p];
	}

	status = mh_ui_atlas_add(
		&hud->atlas, &image, &hud->sprites[id], error_out);
	free(image.rgba);

	return status;
}

static enum mh_error_code
add_text(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	static const struct hud_label labels[] = {{MH_HUD_TOTAL, "TOTAL"},
		{MH_HUD_TIME, "TIME"}, {MH_HUD_LAP, "LAP"},
		{MH_HUD_BEST, "BEST"}, {MH_HUD_POS, "POS"},
		{MH_HUD_RACE, "RACE"}, {MH_HUD_LAST, "LAST"},
		{MH_HUD_EVER, "EVER"}};

	struct mh_font font;
	struct mh_ui_region region;
	unsigned int i;
	enum mh_error_code status;

	status = mh_font_load(fonts, resources, root, MH_ASSET_FONT1S_LOWERCASE,
		&font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_atlas_add(&hud->atlas, &font.image, &region, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < MH_FONT_GLYPHS; i++) {
			hud->advance[i] = hud->hardware_advance[i] =
				font.advance[i];
			hud->glyphs[i].x = region.x + i % 16 * font.cell_width;
			hud->glyphs[i].y = region.y + i / 16 * font.height;
			hud->glyphs[i].width = font.advance[i];
			hud->glyphs[i].height = font.height;
			hud->hardware_glyphs[i] = hud->glyphs[i];
		}

		hud->hardware_cell_width = font.cell_width;
		hud->hardware_height = font.height;
	}

	for (i = 0; status == MH_ERROR_NONE &&
		i < sizeof(labels) / sizeof(labels[0]);
		i++)
		status = add_label(
			&font, labels[i].text, hud, labels[i].id, error_out);

	free(font.image.rgba);

	return status;
}

enum mh_error_code
mh_hud_assets_load_software_from(
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_hud_assets *output,
	struct mh_error *error_out)
{
	struct mh_software_hud_assets source;
	struct mh_hud_assets hud;
	enum mh_error_code status;

	memset(&hud, 0, sizeof(hud));
	status = mh_software_hud_assets_load_from(sprites, fonts, resources,
		root, track, 640, 6, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	hud.atlas = source.atlas;
	copy_instruments(&source, &hud);
	status = add_map(&source, &hud, error_out);
	source.atlas.image.rgba = NULL;
	mh_software_hud_assets_free(&source);

	if (status == MH_ERROR_NONE)
		status = add_text(fonts, resources, root, &hud, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_message_widths_from(
			fonts, resources, root, hud.message_advance, error_out);

	if (status != MH_ERROR_NONE) {
		free(hud.atlas.image.rgba);
		return status;
	}

	*output = hud;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
