#ifndef MH_MENU_BACKGROUND_DRAW_H
#define MH_MENU_BACKGROUND_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_background.h"

struct mh_wireframe;

/*
 * Compose gear/lightning lines and all requested models, in that order, into
 * the indexed canvas. models points to nine already centered/scaled LOBs.
 * Preflights the entire frame; errors leave all pixels unchanged. Combined
 * line count <=65536. Models may be NULL when the frame requests none.
 */
enum mh_error_code	mh_menu_background_render(
	const struct mh_menu_background_draw *draw,
	const struct mh_wireframe *models, unsigned int width,
	unsigned int height, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
