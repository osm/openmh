#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_palette.h"
#include "interfaces/sprite/collection.h"

enum {
	PLAYER_OFFSET = 6 * 3,
	PREVIEW_OFFSET = 128 * 3,
	BACKGROUND_OFFSET = 192 * 3,
	FADE_OFFSET = 255 * 3
};

enum mh_error_code
mh_menu_palette_base(const unsigned char sprites[MH_MENU_PALETTE_BYTES],
	const unsigned char background[MH_MENU_PALETTE_SLICE_BYTES],
	unsigned char output[MH_MENU_PALETTE_BYTES], struct mh_error *error_out)
{
	unsigned char result[MH_MENU_PALETTE_BYTES];

	if (sprites == NULL || background == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing menu palette");

	memcpy(result, sprites, MH_MENU_PALETTE_BYTES);
	memcpy(result + BACKGROUND_OFFSET, background,
		MH_MENU_PALETTE_SLICE_BYTES);

	/*
	 * Original font-color byte overrides. Entries 1 and 64 are partial:
	 * their untouched channels still come from the sprite palette.
	 */
	result[3] = 255;
	result[4] = 100;
	result[6] = result[7] = result[8] = 255;
	result[9] = result[10] = 96;
	result[11] = 192;
	result[12] = result[13] = 186;
	result[14] = 248;
	result[15] = result[16] = result[17] = 0;
	result[194] = 20;
	memcpy(output, result, MH_MENU_PALETTE_BYTES);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_palette_frame(const unsigned char sprites[MH_MENU_PALETTE_BYTES],
	const unsigned char background[MH_MENU_PALETTE_SLICE_BYTES],
	const unsigned char *preview, float fade,
	unsigned char output[MH_MENU_PALETTE_BYTES], struct mh_error *error_out)
{
	unsigned char result[MH_MENU_PALETTE_BYTES];
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL || !(fade >= 0 && fade <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu palette fade");

	status = mh_menu_palette_base(sprites, background, result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_MENU_PALETTE_SLICE_BYTES; i++)
		result[PREVIEW_OFFSET + i] = preview == NULL
			? 0
			: (unsigned char)((double)preview[i] * fade);

	result[FADE_OFFSET] = (unsigned char)(224.0 * fade);
	result[FADE_OFFSET + 1] = (unsigned char)(248.0 * fade);
	result[FADE_OFFSET + 2] = (unsigned char)(255.0 * fade);
	memcpy(output, result, MH_MENU_PALETTE_BYTES);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_palette_submit(unsigned char base[MH_MENU_PALETTE_BYTES],
	unsigned long player_color,
	unsigned char presented[MH_MENU_PALETTE_BYTES],
	struct mh_error *error_out)
{
	if (base == NULL || presented == NULL || base == presented ||
		player_color > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu palette submission");

	base[PLAYER_OFFSET] = (unsigned char)((player_color >> 16) & 255UL);
	base[PLAYER_OFFSET + 1] = (unsigned char)((player_color >> 8) & 255UL);
	base[PLAYER_OFFSET + 2] = (unsigned char)(player_color & 255UL);
	memcpy(presented, base, MH_MENU_PALETTE_BYTES);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_palette_load(const struct mh_sprite_collection *collection,
	unsigned char output[MH_MENU_PALETTE_BYTES], struct mh_error *error_out)
{
	struct mh_image_palette palette;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing menu sprite palette output");

	status = mh_sprite_collection_palette(collection, &palette, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (palette.count != MH_IMAGE_PALETTE_COLORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"menu sprites require a complete palette");

	memcpy(output, palette.rgb, MH_MENU_PALETTE_BYTES);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
