#ifndef MH_MENU_SLIDER_H
#define MH_MENU_SLIDER_H

#include "engine/core/error.h"
#include "engine/ui/menu_text.h"

float	mh_menu_slider_step(unsigned long elapsed_ms, unsigned int shift,
	unsigned int control, unsigned int alt);

/*
 * Integer value/suffix aligned using maximum's text width, then F5/F3/F4 bar
 * glyphs and F2 cursor. The value is not clamped to the range. Finite
 * values/min/max within +/-1e6, min<max, width<=4096, suffix<=8 bytes, and
 * resulting cursor within +/-262144. Invalid calls preserve all pixels. active
 * selects palette 2 instead of 1.
 */
enum mh_error_code	mh_menu_slider_draw(const struct mh_font *font,
	float value, float minimum, float maximum, const char *suffix,
	unsigned int width, int active, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
