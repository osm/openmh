#ifndef MH_MENU_MAIN_H
#define MH_MENU_MAIN_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_choices.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_MAIN_SLOTS 11
#define MH_MENU_MAIN_FRAMES 8
#define MH_MENU_MAIN_LAST_CHOICE 4

struct mh_menu_main_state {
	unsigned int	selected;
	unsigned long	exit_answer;
};

/*
 * Main page binds slots 1..10; slot 4 is deliberately absent. Frames retain
 * index/coverage pairs so the menu can apply its shared palette later.
 */
struct mh_menu_main_art {
	struct mh_sprite_indexed images[MH_MENU_MAIN_SLOTS]
				[MH_MENU_MAIN_FRAMES];
	unsigned int	count[MH_MENU_MAIN_SLOTS];
};

struct mh_menu_main_animation {
	struct mh_menu_sprite_state	sprites;
	struct mh_menu_needle		needle;
};

/*
 * Main-page input. Ordered callback requests, not raw held keys. Dialog
 * creation clears the retained answer only for CONFIRM_EXIT; modal input
 * supplies its eventual answer on a later frame. The adapter must process every
 * event in order: PAGE -> request(), DIALOG -> modal creation. Sound events
 * from those callees are separate from this caller's requests. Invalid
 * state/arguments leave all outputs unchanged.
 */
enum mh_error_code	mh_menu_main_update(struct mh_menu_main_state *state,
	const struct mh_menu_input *input, struct mh_menu_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

/*
 * Decode the nine original main-page names. Missing entries remain absent. At
 * most eight frames per entry and 32 MiB total decoded bytes. Output is owned
 * and unchanged on failure; free it before reusing.
 */
enum mh_error_code	mh_menu_main_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_main_art *output, struct mh_error *error_out);
void			mh_menu_main_art_free(struct mh_menu_main_art *art);

/*
 * Reset all timers/wobble, enable slots 2/5, and aim the needle at
 * 8*X(selected+24)+Y(selected+24), response 32. Position mix is 0..256.
 */
enum mh_error_code	mh_menu_main_animation_init(
	const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, struct mh_menu_main_animation *output,
	struct mh_error *error_out);

/*
 * Animate the selected label, draw all ten slots in order, then the two needle
 * triangles. State, shared RNG/noise and canvas are atomic on errors. Sprite
 * jitter noise is distinct from the shared RNG. Clock and elapsed are unsigned
 * 32-bit milliseconds.
 */
enum mh_error_code	mh_menu_main_render(const struct mh_menu_main_art *art,
	const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_main_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

#endif
