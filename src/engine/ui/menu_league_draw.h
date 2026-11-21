#ifndef MH_MENU_LEAGUE_DRAW_H
#define MH_MENU_LEAGUE_DRAW_H

#include "engine/core/error.h"
#include "engine/game/league.h"
#include "engine/ui/menu_league.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_single.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_LEAGUE_ART_SLOTS 17
#define MH_MENU_LEAGUE_ART_FRAMES 8

struct mh_menu_league_driver {
	const char	*nick;
	unsigned long	color, points, wins, second_places, third_places;
};

/*
 * A borrowed display snapshot, independent of file ownership and driver IDs.
 * drivers are the current division's ordered rows (2 for stage 0, else 8).
 * player/points are the human's nickname and score. created is displayed as
 * the original signed decimal word, not an OS-formatted date.
 */
struct mh_menu_league_info {
	const char			*name, *player;
	unsigned long			created, race_index, points;
	unsigned int			stage;
	struct mh_menu_league_driver	drivers[MH_LEAGUE_DIVISION_SIZE];
};

struct mh_menu_league_details {
	/* state.count entries. */
	const struct mh_menu_league_info	*leagues;
	const struct mh_menu_single_item	*cars, *tracks;
	unsigned int	car_count, track_count, profile_count;
};

struct mh_menu_league_art {
	unsigned int	page, count[MH_MENU_LEAGUE_ART_SLOTS];
	struct mh_sprite_indexed images[MH_MENU_LEAGUE_ART_SLOTS]
				[MH_MENU_LEAGUE_ART_FRAMES];
};

struct mh_menu_league_animation {
	struct mh_menu_sprite_state	sprites;
	struct mh_menu_needle		needle;
};

/*
 * page is MH_MENU_LEAGUE_SETUP/STANDINGS/LOAD/NEW/DELETE. Original slots,
 * including intentional holes, with <=8 frames and <=32 MiB decoded bytes.
 * Missing names remain absent. Owned successful output must be freed.
 */
enum mh_error_code	mh_menu_league_art_read(
	const struct mh_sprite_collection *collection, unsigned int page,
	struct mh_menu_league_art *output, struct mh_error *error_out);
void			mh_menu_league_art_free(struct mh_menu_league_art *art);
enum mh_error_code	mh_menu_league_animation_init(unsigned int page,
	const struct mh_menu_layout *layout, unsigned int choice,
	unsigned int mix, struct mh_menu_league_animation *output,
	struct mh_error *error_out);

/*
 * Complete original page draw, including standings palette entries 128..135
 * and its car-eligibility clamp. Other pages retain car and palette. All
 * pixels, animation, car, palette, RNG/noise commit together on success.
 * Caller builds the current display snapshot before calling. New-page name
 * comes from state.name; editing/cursor happens after this draw.
 */
enum mh_error_code	mh_menu_league_render(
	const struct mh_menu_league_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_league_state *menu,
	const struct mh_menu_league_details *details, unsigned int *car,
	unsigned char palette[MH_MENU_PALETTE_BYTES], unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_league_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);
enum mh_error_code	mh_menu_league_cursor(
	const struct mh_menu_layout *layout, unsigned int mix,
	const struct mh_menu_league_output *input_result,
	const struct mh_font *font, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
