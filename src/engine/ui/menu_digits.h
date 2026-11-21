#ifndef MH_MENU_DIGITS_H
#define MH_MENU_DIGITS_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"
#include "interfaces/sprite/pixels.h"

#define MH_MENU_DIGIT_SLOTS 64

struct mh_menu_digits {
	float		position[MH_MENU_DIGIT_SLOTS];

	/* Caller resets to zero at the start of each frame. */
	unsigned int	cursor;
};

/*
 * Rolling decimal strips, least significant digit first. numa is 19x220, numb
 * 6x40; missing sprites are silent, like original lookup. snap_digits snaps
 * before drawing when exactly 1. State/canvas are atomic. count 0..10, value
 * and elapsed unsigned 32-bit, X -65536..65280, Y +/-65536. Positions and
 * intermediate unsigned conversions are bounded for safety.
 */
enum mh_error_code	mh_menu_digits_draw(
	const struct mh_sprite_indexed *numa,
	const struct mh_sprite_indexed *numb, unsigned long value,
	unsigned int count, long x, long y, unsigned long elapsed_ms,
	unsigned int snap_digits, struct mh_menu_digits *state,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
