#ifndef MH_MENU_RANKINGS_DRAW_H
#define MH_MENU_RANKINGS_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_rankings.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_RANKINGS_ART_SLOTS 11
#define MH_MENU_RANKINGS_ART_FRAMES 8

struct mh_menu_rankings_art {
	struct mh_sprite_indexed images[MH_MENU_RANKINGS_ART_SLOTS]
				[MH_MENU_RANKINGS_ART_FRAMES],
		marker[MH_MENU_RANKINGS_ART_FRAMES];
	unsigned int	count[MH_MENU_RANKINGS_ART_SLOTS], marker_count;
};

struct mh_menu_rankings_animation {
	struct mh_menu_sprite_state	sprites;
	unsigned int			frames[MH_MENU_LAYOUT_SLOTS];
};

enum mh_error_code	mh_menu_rankings_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_rankings_art *output, struct mh_error *error_out);
void			mh_menu_rankings_art_free(
	struct mh_menu_rankings_art *art);

/* Reset timers and enable wobble only for slots 2/3. */
void	mh_menu_rankings_animation_init(
	struct mh_menu_rankings_animation *state);

/*
 * Layout 11: ten sprites, four lists and ten score rows. NULL table omits rows,
 * like the original failed loader. Raw disk text is bounded to its 24/16-byte
 * field even when it has no NUL. All state, RNG/noise and canvas outputs are
 * preserved on failure.
 */
enum mh_error_code	mh_menu_rankings_render(
	const struct mh_menu_rankings_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_rankings_state *menu, const char *const *tracks,
	unsigned int track_count, const struct mh_highscore_table *table,
	unsigned int mix, struct mh_menu_rankings_animation *state,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
