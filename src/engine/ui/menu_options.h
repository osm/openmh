#ifndef MH_MENU_OPTIONS_H
#define MH_MENU_OPTIONS_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_choices.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_OPTIONS_SLOTS 12
#define MH_MENU_OPTIONS_FRAMES 17

struct mh_menu_options_state {
	unsigned int	selected, difficulty;
};

struct mh_menu_options_art {
	struct mh_sprite_indexed images[MH_MENU_OPTIONS_SLOTS]
				[MH_MENU_OPTIONS_FRAMES];
	unsigned int	count[MH_MENU_OPTIONS_SLOTS];
};

struct mh_menu_options_animation {
	struct mh_menu_sprite_state	sprites;
	struct mh_menu_needle		needle;
	unsigned int			difficulty_frame;
};

/*
 * Left, Right, Enter, Escape. Enter on row 4 cycles difficulty; rows 0..3
 * request Personal, Graphics, Controls, Sound. Up/Down do nothing. All events
 * remain ordered, including Enter followed by Escape.
 */
enum mh_error_code	mh_menu_options_update(
	struct mh_menu_options_state *state, const struct mh_menu_input *input,
	struct mh_menu_output *output, const struct mh_menu_choices *choices,
	struct mh_error *error_out);
enum mh_error_code	mh_menu_options_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_options_art *output, struct mh_error *error_out);
void			mh_menu_options_art_free(
	struct mh_menu_options_art *art);

/* Reset sprite timers/flags and needle, retain difficulty follower. */
enum mh_error_code	mh_menu_options_animation_init(
	const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, struct mh_menu_options_animation *state,
	struct mh_error *error_out);

/* Layout 7. Invalid requests preserve pixels/state/RNG. */
enum mh_error_code	mh_menu_options_render(
	const struct mh_menu_options_art *art,
	const struct mh_menu_layout *layout,
	const struct mh_menu_options_state *menu, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_options_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

#endif
