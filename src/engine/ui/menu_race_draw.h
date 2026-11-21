#ifndef MH_MENU_RACE_DRAW_H
#define MH_MENU_RACE_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_digits.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_race_setup.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_thumbnail.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/geometry/wireframe.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_RACE_ART_FRAMES 8
#define MH_MENU_RACE_ART_SLOTS 11

struct mh_menu_race_art {
	struct mh_sprite_indexed images[MH_MENU_RACE_ART_SLOTS]
				[MH_MENU_RACE_ART_FRAMES],
		numa, numb;
	unsigned int	count[MH_MENU_RACE_ART_SLOTS];
	unsigned char	palette[MH_MENU_PALETTE_BYTES];
};

struct mh_menu_race_animation {
	struct mh_menu_sprite_state	sprites;
	float				fade;
	unsigned int			shown_track, catch_frame;
};

struct mh_menu_race_scene {
	const char		*const *names;

	const struct mh_menu_thumbnail
		*thumbnails; /* NULL pixels mean absent. */
	const struct mh_wireframe
		*models; /* Already centered and fitted in catalog order. */
	unsigned int		count, unlocked, mode;
	const unsigned char	*background_palette; /* 64 RGB triples. */
};

enum mh_error_code	mh_menu_race_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_race_art *output, struct mh_error *error_out);
void			mh_menu_race_art_free(struct mh_menu_race_art *art);

/* Reset sprites and fade, retaining shown_track and catch_frame. */
void	mh_menu_race_animation_init(struct mh_menu_race_animation *state);

/*
 * Complete layout-2 composition. The caller resets digits.cursor once per
 * frontend frame. snap_digits snaps when exactly 1. The viewport is 640x400;
 * canvas includes offscreen padding. Pixels, palette, animation, digits, RNG
 * and row noise commit together.
 */
enum mh_error_code	mh_menu_race_render(const struct mh_menu_race_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_race_setup_state *menu,
	const struct mh_menu_race_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms,
	unsigned int snap_digits, struct mh_menu_race_animation *state,
	struct mh_menu_digits *digits, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
