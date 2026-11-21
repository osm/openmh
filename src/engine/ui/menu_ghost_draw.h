#ifndef MH_MENU_GHOST_DRAW_H
#define MH_MENU_GHOST_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_ghost.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_GHOST_ART_SLOTS 11
#define MH_MENU_GHOST_ART_FRAMES 8

struct mh_menu_ghost_art {
	struct mh_sprite_indexed images[MH_MENU_GHOST_ART_SLOTS]
				[MH_MENU_GHOST_ART_FRAMES],
		marker[MH_MENU_GHOST_ART_FRAMES];
	unsigned int	count[MH_MENU_GHOST_ART_SLOTS], marker_count;
	int		patched;
};

struct mh_menu_ghost_animation {
	struct mh_menu_sprite_state	sprites;
	unsigned int frames[MH_MENU_LAYOUT_SLOTS]; /* Original shared frame
				registers, retained on entry. */
};

enum mh_error_code	mh_menu_ghost_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_ghost_art *output, struct mh_error *error_out);
void			mh_menu_ghost_art_free(struct mh_menu_ghost_art *art);
void			mh_menu_ghost_animation_init(
	struct mh_menu_ghost_animation *state);

/*
 * Draw slots using retained frames FIRST, then set mode and next-frame
 * registers from focus, draw the recording list and hint. names has menu->count
 * entries. Updates mode, animation, RNG/noise and pixels atomically. Original
 * layout index is 19. Patched art adds tbr at slot 6 and moves fghos to slot
 * 10; menu->patched must match art->patched.
 */
enum mh_error_code	mh_menu_ghost_render(
	const struct mh_menu_ghost_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout, struct mh_menu_ghost_state *menu,
	const char *const *names, unsigned int mix, unsigned long clock_ms,
	unsigned long elapsed_ms, struct mh_menu_ghost_animation *state,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
