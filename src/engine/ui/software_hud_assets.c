#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_fonts.h"
#include "engine/assets/path_hud.h"
#include "engine/ui/software_hud_assets.h"
#include "interfaces/font/source.h"
#include "interfaces/sprite/collection.h"

enum {
	MIN_WIDTH = 320,
	SMALL_WIDTH = 640,
	VIEW_LIMIT = 16384,
	ATLAS_WIDTH = 1024,
	ATLAS_HEIGHT = 2048
};

static enum mh_error_code
add_sprite(struct mh_software_hud_assets *assets,
	const struct mh_sprite_collection *archive, const char *name,
	unsigned int frame, struct mh_ui_region *region,
	struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	status = mh_sprite_collection_rgba(
		archive, name, frame, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_atlas_add(&assets->atlas, &image, region, error_out);
	free(image.rgba);

	return status;
}

static enum mh_error_code
add_sprites(const struct mh_sprite_collection *archive,
	struct mh_software_hud_assets *assets, struct mh_error *error_out)
{
	static const char *const names[] = {"meter", "meter", "count", "count",
		"count", "count", "kolon", "ghost", "r", "cam", "tgear",
		"ttott", "tlast", "tpos", "tkmh", "tlap", "tlapt", "tble",
		"tof", "tww", "tblt", "trt", "tbrte", "minus", "plus", "snumr",
		"tkmh"};

	unsigned int i, frame;
	char number[8];
	const char *name;
	enum mh_error_code status;

	for (i = 0; i < MH_SOFTWARE_HUD_SPRITES; i++) {
		frame = 0;

		if (i < MH_SOFTWARE_HUD_METER) {
			sprintf(number, "%cnum%u",
				i < MH_SOFTWARE_HUD_BIG ? 's' : 'b', i % 10);
			name = number;
		} else {
			name = names[i - MH_SOFTWARE_HUD_METER];

			if (i == MH_SOFTWARE_HUD_METER_EMPTY)
				frame = 1;
			else if (i >= MH_SOFTWARE_HUD_THREE &&
				i <= MH_SOFTWARE_HUD_GO)
				frame = i - MH_SOFTWARE_HUD_THREE;
		}

		if (i == MH_SOFTWARE_HUD_MPH) {
			struct mh_sprite_info info;

			status = mh_sprite_collection_info(
				archive, name, &info, error_out);

			if (status == MH_ERROR_NOT_FOUND) {
				status = mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
				continue;
			}

			if (status != MH_ERROR_NONE)
				return status;

			if (info.frames < 2)
				continue;

			frame = 1;
		}

		status = add_sprite(assets, archive, name, frame,
			&assets->sprites[i], error_out);

		if (status != MH_ERROR_NONE)
			break;
	}

	return status;
}

static enum mh_error_code
load_sprites(const struct mh_sprite_source_provider *sprites,
	const struct mh_resource_source *resources, const char *root,
	unsigned int bank, struct mh_software_hud_assets *assets,
	struct mh_error *error_out)
{
	struct mh_sprite_collection *archive;
	char relative[32];
	enum mh_error_code status;

	sprintf(relative, MH_ASSET_HUD_SOFTWARE_SPRITES_FORMAT,
		(int)('0' + bank));
	status = mh_sprite_collection_open(sprites, resources, root, relative,
		assets->green_bits == 6 ? MH_PICTURE_RGB565 : MH_PICTURE_RGB555,
		&archive, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = add_sprites(archive, assets, error_out);
	mh_sprite_collection_close(archive);

	return status;
}

static enum mh_error_code
load_map(const struct mh_sprite_source_provider *sprites,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, unsigned int bank,
	struct mh_software_hud_assets *assets, struct mh_error *error_out)
{
	struct mh_sprite_collection *archive;
	char relative[MH_TRACK_PATH_SIZE + 8], name[8];
	unsigned int i;
	enum mh_error_code status;

	if (track == NULL || (track->map_name[0] == 0 && !track->has_map_info))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	sprintf(relative, "%s%c.dta", track->map_name, (int)('0' + bank));
	status = mh_sprite_collection_open(sprites, resources, root, relative,
		assets->green_bits == 6 ? MH_PICTURE_RGB565 : MH_PICTURE_RGB555,
		&archive, error_out);

	if (status == MH_ERROR_NONE) {
		status = add_sprite(assets, archive,
			assets->resolution_shift ? "mapl" : "maph", 0,
			&assets->map, error_out);

		for (i = 0;
			i < MH_SOFTWARE_HUD_RANKS && status == MH_ERROR_NONE;
			i++) {
			sprintf(name, "%ss%c",
				assets->resolution_shift ? "low" : "hig",
				(int)('1' + i));
			status = add_sprite(assets, archive, name, 0,
				&assets->small_markers[i], error_out);

			if (status != MH_ERROR_NONE)
				break;

			sprintf(name, "%sb%c",
				assets->resolution_shift ? "low" : "hig",
				(int)('1' + i));
			status = add_sprite(assets, archive, name, 0,
				&assets->large_markers[i], error_out);
		}

		mh_sprite_collection_close(archive);
	}

	if (status == MH_ERROR_NONE)
		memcpy(assets->map_info, track->map_info,
			sizeof(assets->map_info));

	return status;
}

void
mh_software_hud_assets_free(struct mh_software_hud_assets *assets)
{
	if (assets == NULL)
		return;

	free(assets->atlas.image.rgba);
	free(assets->font.bytes);
	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_software_hud_assets_load_from(
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, unsigned int width,
	unsigned int green_bits, struct mh_software_hud_assets *output,
	struct mh_error *error_out)
{
	struct mh_software_hud_assets assets;
	struct mh_image image;
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned int bank;
	enum mh_error_code status;

	if (root == NULL || output == NULL || width < MIN_WIDTH ||
		width > VIEW_LIMIT || (green_bits != 5 && green_bits != 6) ||
		(track != NULL &&
			(memchr(track->map_name, 0, sizeof(track->map_name)) ==
					NULL ||
				((track->map_name[0] == 0) !=
					(track->has_map_info == 0)))))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD assets or map configuration");

	memset(&assets, 0, sizeof(assets));
	assets.resolution_shift = width < SMALL_WIDTH ? 1 : 0;
	assets.green_bits = green_bits;
	bank = assets.resolution_shift * 2 + (green_bits == 6);
	status = mh_ui_atlas_init(
		ATLAS_WIDTH, ATLAS_HEIGHT, &assets.atlas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_sprites(
		sprites, resources, root, bank, &assets, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_font_load_program(fonts, resources, root,
			assets.resolution_shift ? MH_ASSET_FONT2S_LOWERCASE
						: MH_ASSET_FONT2_LOWERCASE,
			&assets.font, error_out);

	if (status == MH_ERROR_NONE)
		status = load_map(sprites, resources, root, track, bank,
			&assets, error_out);

	if (status == MH_ERROR_NONE) {
		image.width = image.height = 1;
		image.rgba = white;
		status = mh_ui_atlas_add(
			&assets.atlas, &image, &assets.solid, error_out);
	}

	if (status != MH_ERROR_NONE) {
		mh_software_hud_assets_free(&assets);
		return status;
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
