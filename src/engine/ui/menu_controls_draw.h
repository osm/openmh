#ifndef MH_MENU_CONTROLS_DRAW_H
#define MH_MENU_CONTROLS_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_choices.h"
#include "engine/ui/menu_controls.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_text.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_CONTROLS_ART_SLOTS 26
#define MH_MENU_CONTROLS_ART_FRAMES 8

struct mh_menu_controls_art {
	struct mh_sprite_indexed images[MH_MENU_CONTROLS_ART_SLOTS]
				[MH_MENU_CONTROLS_ART_FRAMES];
	unsigned int	count[MH_MENU_CONTROLS_ART_SLOTS];
	unsigned int	preferences;
};

struct mh_menu_controls_animation {
	struct mh_menu_sprite_state	sprites;
};

/*
 * Preference labels in slots 5..8. Preference artwork selects fourteen rows;
 * legacy artwork keeps ten. Missing names remain absent. Init clears sprite
 * effects, not menu focus.
 */
enum mh_error_code	mh_menu_controls_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_controls_art *output, struct mh_error *error_out);
void			mh_menu_controls_art_free(
	struct mh_menu_controls_art *art);
void			mh_menu_controls_animation_init(
	struct mh_menu_controls_animation *state);

/*
 * Layout 8, ten action rows, low-byte key names and a list centred on the
 * selected profile. Draw precedes input. Capture highlights the selected key
 * name regardless of the active column. Patched capture prompts replace that
 * name: Release: <held key> while waiting for release, then Assign new control
 * while waiting for a binding. Patched artwork adds feedback and X/Y/Z
 * sensitivity rows before actions. Unavailable preferences show a label and
 * preserve their saved values. NULL key_names selects the stable native
 * catalog; otherwise the adapter supplies 256 bounded (at most 31 bytes plus
 * NUL) legacy labels. All pixel, sprite, RNG and noise changes commit together;
 * errors preserve outputs.
 */
enum mh_error_code	mh_menu_controls_render(
	const struct mh_menu_controls_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_controls_state *menu,
	const struct mh_control_profile *profiles, unsigned int count,
	const char *const *key_names, unsigned int mix, unsigned long clock_ms,
	struct mh_menu_controls_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

#endif
