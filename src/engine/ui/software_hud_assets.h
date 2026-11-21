#ifndef MH_SOFTWARE_HUD_ASSETS_H
#define MH_SOFTWARE_HUD_ASSETS_H

#include "engine/ui/atlas.h"
#include "interfaces/catalog/track.h"
#include "interfaces/font/glyphs.h"

struct mh_sprite_source_provider;
struct mh_font_source_provider;

struct mh_resource_source;

enum mh_software_hud_sprite {
	MH_SOFTWARE_HUD_SMALL,
	MH_SOFTWARE_HUD_BIG = 10,
	MH_SOFTWARE_HUD_METER = 20,
	MH_SOFTWARE_HUD_METER_EMPTY,
	MH_SOFTWARE_HUD_THREE,
	MH_SOFTWARE_HUD_TWO,
	MH_SOFTWARE_HUD_ONE,
	MH_SOFTWARE_HUD_GO,
	MH_SOFTWARE_HUD_COLON,
	MH_SOFTWARE_HUD_GHOST,
	MH_SOFTWARE_HUD_REVERSE,
	MH_SOFTWARE_HUD_CAMERA,
	MH_SOFTWARE_HUD_GEAR,
	MH_SOFTWARE_HUD_TOTAL_TIME,
	MH_SOFTWARE_HUD_LAST_LAP_TIME,
	MH_SOFTWARE_HUD_POSITION,
	MH_SOFTWARE_HUD_KMH,
	MH_SOFTWARE_HUD_LAP,
	MH_SOFTWARE_HUD_LAP_TIME,
	MH_SOFTWARE_HUD_BEST_LAP_EVER,
	MH_SOFTWARE_HUD_SLASH,
	MH_SOFTWARE_HUD_WRONG_WAY,
	MH_SOFTWARE_HUD_BEST_LAP_TIME,
	MH_SOFTWARE_HUD_RACE_TIME,
	MH_SOFTWARE_HUD_BEST_RACE_EVER,
	MH_SOFTWARE_HUD_MINUS,
	MH_SOFTWARE_HUD_PLUS,
	MH_SOFTWARE_HUD_REVERSE_SMALL,
	MH_SOFTWARE_HUD_MPH,
	MH_SOFTWARE_HUD_SPRITES
};

#define MH_SOFTWARE_HUD_RANKS 8

struct mh_software_hud_assets {
	struct mh_ui_atlas	atlas;
	struct mh_font_program	font;
	struct mh_ui_region	sprites[MH_SOFTWARE_HUD_SPRITES];
	struct mh_ui_region map, small_markers[MH_SOFTWARE_HUD_RANKS],
		large_markers[MH_SOFTWARE_HUD_RANKS], solid;
	float			map_info[4];
	unsigned int		resolution_shift, green_bits;
};

/*
 * Original GAME0..3 and track DTA selection: width <640 adds 2, green_bits=6
 * adds 1. Fonts are FONT2S below 640, otherwise FONT2, with byte writes
 * retained. width is 320..16384, green_bits 5/6. A NULL track or fully absent
 * map config omits maps; incomplete config fails. Output is unchanged on
 * failure. Owns atlas/font allocations. Supply empty output; free before
 * reloading. Borrowed UI font commands must finish before freeing these assets.
 */
enum mh_error_code	mh_software_hud_assets_load_from(
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	const struct mh_track *track, unsigned int width,
	unsigned int green_bits, struct mh_software_hud_assets *output,
	struct mh_error *error_out);
void			mh_software_hud_assets_free(
	struct mh_software_hud_assets *assets);

#endif
