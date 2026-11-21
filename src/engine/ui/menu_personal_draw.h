#ifndef MH_MENU_PERSONAL_DRAW_H
#define MH_MENU_PERSONAL_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_personal.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_text.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_PERSONAL_ART_SLOTS 4

struct mh_menu_personal_art {
	int				patched;
	struct mh_sprite_indexed	images[MH_MENU_PERSONAL_ART_SLOTS];
};

struct mh_menu_personal_animation {
	struct mh_menu_sprite_state	sprites;
};

/* Dots/opt/scrd in slots 1..3; missing names remain absent. */
enum mh_error_code	mh_menu_personal_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_personal_art *output, struct mh_error *error_out);
void			mh_menu_personal_art_free(
	struct mh_menu_personal_art *art);
void			mh_menu_personal_animation_init(
	struct mh_menu_personal_animation *state);

/*
 * Draw sprites, labels, then PRE-INPUT text values and publish the 32 choices
 * into palette 128..159. Layout 17, slots 20/21. Colors are Default.ACT
 * indices 255..224. Preserves all outputs on any error.
 */
enum mh_error_code	mh_menu_personal_render(
	const struct mh_menu_personal_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_personal_state *menu,
	const struct mh_settings *settings,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS], unsigned int mix,
	struct mh_menu_personal_animation *state, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * Input's drawing follows the draw callback: old-color swatches with NEW
 * focus, followed by the post-edit cursor. Call with update's output only
 * when input ran; leaving/modal frames have no widgets/cursor. Palette
 * 160..163 and complete pixels commit together. No settings mutations.
 */
enum mh_error_code	mh_menu_personal_overlay(const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_personal_output *output, unsigned int mix,
	unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
