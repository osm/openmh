#ifndef MH_MENU_SINGLE_DRAW_H
#define MH_MENU_SINGLE_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_choices.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_single.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_SINGLE_ART_SLOTS 12
#define MH_MENU_SINGLE_ART_FRAMES 8

struct mh_menu_single_art {
	struct mh_sprite_indexed images[MH_MENU_SINGLE_ART_SLOTS]
				[MH_MENU_SINGLE_ART_FRAMES];
	unsigned int	count[MH_MENU_SINGLE_ART_SLOTS];
};

struct mh_menu_single_animation {
	struct mh_menu_sprite_state	sprites;
	struct mh_menu_needle		needle;

	/* Publish selections after drawing; inherited by Ghost. */
	unsigned int			frames[MH_MENU_LAYOUT_SLOTS];
};

/*
 * Eleven named entries, <=8 frames each, <=32 MiB decoded total. Missing
 * entries remain absent. Free successful owned output before reuse.
 */
enum mh_error_code	mh_menu_single_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_single_art *output, struct mh_error *error_out);
void			mh_menu_single_art_free(struct mh_menu_single_art *art);
enum mh_error_code	mh_menu_single_animation_init(
	const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, struct mh_menu_single_animation *output,
	struct mh_error *error_out);

/*
 * Draw slots 1..11, needle, then information panel with FONT1. Shared RNG and
 * row noise retain their order. All pixels, animation, RNG and menu selection
 * changes are committed together on success.
 */
enum mh_error_code	mh_menu_single_render(
	const struct mh_menu_single_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout, struct mh_menu_single_state *menu,
	const struct mh_menu_single_details *details, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_single_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

#endif
