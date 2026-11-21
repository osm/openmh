#ifndef MH_MENU_MULTI_DRAW_H
#define MH_MENU_MULTI_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_multi.h"
#include "engine/ui/menu_sprite.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MULTI_ART_SLOTS 29
#define MH_MULTI_ART_FRAMES 8

struct mh_menu_multi_art {
	unsigned int	count[MH_MULTI_ART_SLOTS];
	struct mh_sprite_indexed images[MH_MULTI_ART_SLOTS]
				[MH_MULTI_ART_FRAMES];
};

struct mh_menu_multi_animation {
	struct mh_menu_sprite_state	sprites;
	unsigned int			frames[MH_MENU_LAYOUT_SLOTS];
};

struct mh_menu_multi_address {
	const char	*name, *ip;
};

struct mh_menu_multi_catalog {
	const struct mh_menu_multi_address	*addresses;
	const char				*const *sessions;
	unsigned int	address_count, session_count, protected_count;
};

/*
 * Decode the artwork bindings used by TCP/IP, retaining missing names as
 * absent. Animation keeps timers and wobble between frames.
 */
enum mh_error_code	mh_menu_multi_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_multi_art *output, struct mh_error *error_out);
void			mh_menu_multi_art_free(struct mh_menu_multi_art *art);
void			mh_menu_multi_animation_init(
	struct mh_menu_multi_animation *state);

/*
 * Layout 9, with address/session panels and record list. Catalog strings allow
 * empty byte names, bounded to 255 bytes. Catalog consists of TCP/IP entries
 * only. Protected search rows appear separately when present; the menu omits
 * local search. Counts <=4096, 0 <= protected_count <= address_count. An empty
 * catalog shows an add-entry prompt. Rendering clamps stale address/session
 * indices and commits those changes only on success. All pixels, animation,
 * RNG/noise and selection remain unchanged on error.
 */
enum mh_error_code	mh_menu_multi_render(
	const struct mh_menu_multi_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout, struct mh_menu_multi_state *menu,
	const struct mh_settings *settings,
	const struct mh_menu_multi_catalog *catalog, unsigned int mix,
	unsigned long clock_ms, struct mh_menu_multi_animation *state,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/* Input-time search notice/cursor, applied after render and update. */
enum mh_error_code	mh_menu_multi_overlay(const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_multi_output *output, unsigned int mix,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
