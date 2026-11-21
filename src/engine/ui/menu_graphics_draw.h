#ifndef MH_MENU_GRAPHICS_DRAW_H
#define MH_MENU_GRAPHICS_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_graphics.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_text.h"
#include "interfaces/sprite/collection.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_GRAPHICS_ART_SLOTS 4

struct mh_menu_graphics_art {
	struct mh_sprite_indexed	images[MH_MENU_GRAPHICS_ART_SLOTS];
};

struct mh_menu_graphics_animation {
	struct mh_menu_sprite_state	sprites;
};

/* Dots/opt/scr4, slots 1..3. Missing names remain absent. */
enum mh_error_code	mh_menu_graphics_art_read(
	const struct mh_sprite_collection *collection,
	struct mh_menu_graphics_art *output, struct mh_error *error_out);
void			mh_menu_graphics_art_free(
	struct mh_menu_graphics_art *art);
void			mh_menu_graphics_animation_init(
	struct mh_menu_graphics_animation *state);

/*
 * Layout 5, eight ordinary rows plus device-supported Custom rows. Uses
 * supplied portable device/mode names; enumeration is the adapter's job.
 * Invalid requests preserve all pixels, sprite state and shared RNG/noise.
 * Slider geometry bounds are documented in menu_slider.h.
 */
enum mh_error_code	mh_menu_graphics_render(
	const struct mh_menu_graphics_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_graphics_state *menu,
	const struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog, unsigned int mix,
	struct mh_menu_graphics_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
