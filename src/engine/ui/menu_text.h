#ifndef MH_MENU_TEXT_H
#define MH_MENU_TEXT_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"
#include "interfaces/font/glyphs.h"

/*
 * Draw a left-aligned original byte-font string into the padded allocation.
 * text_size bounds the search for NUL, at most 256 bytes. The decoded atlas
 * supplies glyph coverage and advances; color is an indexed palette entry.
 * No screen-rectangle clipping: only allocation bounds, as in menu dialogs.
 * Validate the complete request before changing pixels.
 */
enum mh_error_code	mh_menu_text_draw(const struct mh_font *font,
	const unsigned char *text, size_t text_size, long x, long y,
	unsigned int color, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
