#ifndef MH_MENU_CREDITS_H
#define MH_MENU_CREDITS_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_text.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_CREDITS_ART_SLOTS 4

struct mh_menu_credits_art {
	struct mh_sprite_indexed	images[MH_MENU_CREDITS_ART_SLOTS];
};

struct mh_menu_credits_animation {
	struct mh_menu_sprite_state	sprites;
};

struct mh_menu_credits_input {
	unsigned int	escape, enter;
	unsigned int control, left_alt,
		right_alt; /* Held left Ctrl and both Alt keys. */
};

/* Dots/scr9/credi, first frames, wobble in slots 2/3. */
enum mh_error_code	mh_menu_credits_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_credits_art *output, struct mh_error *error_out);
void			mh_menu_credits_art_free(
	struct mh_menu_credits_art *art);
void			mh_menu_credits_animation_init(
	struct mh_menu_credits_animation *state);

/*
 * Exact value 1, not any nonzero value. Holding the shortcut changes only this
 * frame; it does not set the Club TBL latch.
 */
int	mh_menu_credits_alternate(int club,
	const struct mh_menu_credits_input *input);

/* Escape (sound 1) then Enter (11); both request Main. */
enum mh_error_code	mh_menu_credits_update(
	const struct mh_menu_credits_input *input,
	struct mh_menu_output *output, struct mh_error *error_out);

/*
 * Layout 18, centred byte-font text at slot 24; row unit is slot 25's Y divided
 * by 8, truncating toward zero. All output is atomic.
 */
enum mh_error_code	mh_menu_credits_render(
	const struct mh_menu_credits_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout, int club,
	const struct mh_menu_credits_input *input, unsigned int mix,
	struct mh_menu_credits_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
