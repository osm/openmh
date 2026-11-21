#ifndef MH_MENU_SOUND_DRAW_H
#define MH_MENU_SOUND_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_digits.h"
#include "engine/ui/menu_sound.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_SOUND_ART_SLOTS 22
#define MH_MENU_SOUND_ART_FRAMES 8

struct mh_menu_sound_art {
	struct mh_sprite_indexed images[MH_MENU_SOUND_ART_SLOTS]
				[MH_MENU_SOUND_ART_FRAMES],
		numa, numb;
	unsigned int	count[MH_MENU_SOUND_ART_SLOTS];
};

struct mh_menu_sound_animation {
	struct mh_menu_sprite_state	sprites;
	unsigned int			loop_frame;
};

struct mh_menu_sound_scene {
	const char		*const *names;
	const unsigned long	*tracks, *seconds;
	unsigned int count,
		seconds_count; /* seconds is indexed by physical track. */
};

enum mh_error_code	mh_menu_sound_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_sound_art *output, struct mh_error *error_out);
void			mh_menu_sound_art_free(struct mh_menu_sound_art *art);

/* Reset sprite animation, retaining the loop-switch follower. */
void	mh_menu_sound_animation_init(struct mh_menu_sound_animation *state);

/*
 * Complete layout-6 compositor. The caller resets digits.cursor once per frame.
 * Drawing consumes menu->list_changed after rendering the selected course,
 * preceding course and following course, in that order. Track numbers outside
 * the supplied length table display duration 00:00. Canvas, state, digits and
 * RNG commit atomically.
 */
enum mh_error_code	mh_menu_sound_render(
	const struct mh_menu_sound_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout, struct mh_menu_sound_state *menu,
	const struct mh_menu_sound_values *values,
	const struct mh_menu_sound_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_sound_animation *state, struct mh_menu_digits *digits,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
