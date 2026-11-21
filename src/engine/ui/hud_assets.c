#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/path_hud.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/image/track_map.h"
#include "engine/storage/asset_path.h"
#include "engine/ui/font_atlas.h"
#include "engine/ui/hud_assets.h"
#include "interfaces/font/source.h"
#include "interfaces/storage/resource.h"

#define ATLAS_WIDTH 1024
#define ATLAS_HEIGHT 2048

static enum mh_error_code
load_hardware_font(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	struct mh_font font;
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;

	status = mh_image_path_from(image_decoder, resources, root,
		MH_ASSET_HUD_FONT_ATLAS, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_hardware_decode_from(
		image_decoder, file.data, file.size, &font, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_font_atlas_add(&hud->atlas, &font, 1,
		hud->hardware_glyphs, hud->hardware_advance, error_out);
	free(font.image.rgba);

	if (status != MH_ERROR_NONE)
		return status;

	hud->hardware_cell_width = font.cell_width;
	hud->hardware_height = font.height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_secret_scroll(const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;
	unsigned int i, length;

	status = mh_asset_path_from(resources, root, MH_ASSET_HUD_SECRET_SCROLL,
		path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.size >= sizeof(hud->secret_scroll)) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"hidden scroll text is too long");
	}

	/*
	 * Original fopen(...,"rb") preserves CR/LF. This .iff is plain text,
	 * not an image; keep every byte for the original font.
	 */
	length = 0;

	for (i = 0; i < file.size && file.data[i] != 0; i++) {
		hud->secret_scroll[length++] = (char)file.data[i];
	}

	hud->secret_scroll[length] = 0;
	hud->secret_scroll_length = length;
	free(file.data);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_hud_assets_message_widths_from(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	unsigned int advances[MH_FONT_GLYPHS], struct mh_error *error_out)
{
	struct mh_font font;
	enum mh_error_code status;

	/*
	 * The race sets software FONT2 to byte mode. Use those advances for
	 * wrapping, even though hardware notifications are drawn with
	 * myfont.iff.
	 */
	status = mh_font_load(fonts, resources, root, MH_ASSET_FONT2_LOWERCASE,
		&font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(advances, font.advance, sizeof(font.advance));
	free(font.image.rgba);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_sprites(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	static const char *const names[] = {MH_ASSET_HUD_KMH_NAME,
		MH_ASSET_HUD_GEAR_NAME, MH_ASSET_HUD_REVERSE_NAME,
		MH_ASSET_HUD_METER_NAME, MH_ASSET_HUD_COLON_NAME,
		MH_ASSET_HUD_SLASH_NAME, MH_ASSET_HUD_TOTAL_NAME,
		MH_ASSET_HUD_TIME_NAME, MH_ASSET_HUD_LAP_NAME,
		MH_ASSET_HUD_BEST_NAME, MH_ASSET_HUD_POSITION_NAME,
		MH_ASSET_HUD_THREE_NAME, MH_ASSET_HUD_TWO_NAME,
		MH_ASSET_HUD_ONE_NAME, MH_ASSET_HUD_GO_NAME,
		MH_ASSET_HUD_WRONG_WAY_NAME, MH_ASSET_HUD_PLAYER_MARKER_NAME,
		MH_ASSET_HUD_OTHER_MARKER_NAME, MH_ASSET_HUD_AVENGER_NAME,
		MH_ASSET_HUD_MAP_1_NAME, MH_ASSET_HUD_MAP_2_NAME,
		MH_ASSET_HUD_MAP_3_NAME, MH_ASSET_HUD_MAP_4_NAME,
		MH_ASSET_HUD_MAP_5_NAME, MH_ASSET_HUD_MAP_6_NAME,
		MH_ASSET_HUD_MAP_7_NAME, MH_ASSET_HUD_MAP_8_NAME,
		MH_ASSET_HUD_RACE_NAME, MH_ASSET_HUD_LAST_NAME,
		MH_ASSET_HUD_EVER_NAME, MH_ASSET_HUD_CAMERA_NAME,
		MH_ASSET_HUD_GHOST_NAME, MH_ASSET_HUD_MPH_NAME};

	struct mh_image image;
	struct mh_image_decode_options options;
	struct mh_file file;
	char relative[128], path[1024];
	enum mh_error_code status;
	unsigned int i;

	options.alpha = MH_IMAGE_ALPHA_MAGENTA;
	options.palette = NULL;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		if (i < MH_HUD_KMH)
			sprintf(relative, MH_ASSET_HUD_DIGIT_FORMAT,
				i < MH_HUD_BIG ? 's' : 'b', i % 10);
		else
			sprintf(relative, MH_ASSET_HUD_IMAGE_FORMAT,
				names[i - MH_HUD_KMH]);
		status = mh_image_path_from(image_decoder, resources, root,
			relative, path, sizeof(path), error_out);

		if (status == MH_ERROR_NOT_FOUND && i == MH_HUD_MPH)
			continue;

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_resource_source_read(
			resources, path, &file, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_image_decode(image_decoder, file.data, file.size,
			&options, &image, NULL, error_out);
		free(file.data);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_ui_atlas_add(
			&hud->atlas, &image, &hud->sprites[i], error_out);
		free(image.rgba);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_font(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *hud, struct mh_error *error_out)
{
	struct mh_font font;
	enum mh_error_code status;

	status = mh_font_load(fonts, resources, root, MH_ASSET_FONT1S_LOWERCASE,
		&font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_font_atlas_add(
		&hud->atlas, &font, 0, hud->glyphs, hud->advance, error_out);
	free(font.image.rgba);

	return status;
}

enum mh_error_code
mh_hud_assets_load_from(const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_hud_assets *output, struct mh_error *error_out)
{
	unsigned char white[4] = {255, 255, 255, 255};
	struct mh_hud_assets hud;
	struct mh_image image;
	enum mh_error_code status;

	memset(&hud, 0, sizeof(hud));

	/*
	 * Race sessions add a track map, result background/font and up to eight
	 * portraits after this HUD. Preserve the shelf packing and sprite
	 * pixels while leaving room for their complete native texture set.
	 */
	status = mh_ui_atlas_init(
		ATLAS_WIDTH, ATLAS_HEIGHT, &hud.atlas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_sprites(image_decoder, resources, root, &hud, error_out);

	if (status == MH_ERROR_NONE)
		status = load_font(fonts, resources, root, &hud, error_out);

	if (status != MH_ERROR_NONE)
		goto failure;

	status = mh_hud_assets_message_widths_from(
		fonts, resources, root, hud.message_advance, error_out);

	if (status != MH_ERROR_NONE)
		goto failure;

	status = load_hardware_font(
		image_decoder, resources, root, &hud, error_out);

	if (status == MH_ERROR_NONE)
		status = load_secret_scroll(resources, root, &hud, error_out);

	if (status == MH_ERROR_NONE) {
		image.width = image.height = 1;
		image.rgba = white;
		status = mh_ui_atlas_add(
			&hud.atlas, &image, &hud.solid, error_out);
	}

	if (status != MH_ERROR_NONE)
		goto failure;

	*output = hud;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

failure:
	free(hud.atlas.image.rgba);

	return status;
}

enum mh_error_code
mh_hud_assets_map_from(const struct mh_image_decoder_provider *image_decoder,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, struct mh_hud_assets *hud,
	struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_ui_region region;
	char normalized[256];
	enum mh_error_code status;
	unsigned int i;

	if (track == NULL || hud == NULL || hud->map.width != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid map asset destination");

	if (track->map_name[0] == 0 && !track->has_map_info)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (track->map_name[0] == 0 || !track->has_map_info)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete track map configuration");

	for (i = 0; i < sizeof(normalized); i++) {
		normalized[i] = track->map_name[i];

		if (normalized[i] == 0)
			break;

		if (normalized[i] >= 'A' && normalized[i] <= 'Z')
			normalized[i] += 'a' - 'A';

		if (normalized[i] == '/')
			normalized[i] = '\\';
	}

	if (i == sizeof(normalized))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"track map name is too long");

	status = mh_track_map_load_from(
		image_decoder, resources, root, track, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_ui_atlas_add(&hud->atlas, &image, &region, error_out);
	free(image.rgba);

	if (status != MH_ERROR_NONE)
		return status;

	hud->map = region;
	memcpy(hud->map_info, track->map_info, sizeof(hud->map_info));
	hud->map_offset[0] = hud->map_offset[1] = 0;

	if (strcmp(normalized, "tracks\\track5\\data\\volvo3") == 0)
		hud->map_offset[0] = -10;

	if (strcmp(normalized, "tracks\\track3\\data\\okkcity") == 0) {
		hud->map_offset[0] = -12;
		hud->map_offset[1] = -2;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
