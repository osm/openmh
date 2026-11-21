#ifndef MH_MENU_DIALOG_DRAW_H
#define MH_MENU_DIALOG_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_noise.h"
#include "interfaces/font/glyphs.h"
#include "interfaces/sprite/pixels.h"

/*
 * Draw using the pre-update modal snapshot. Noise consumes 96 shared RNG
 * values, then req, title, choices and selected highlight draw. Font is the
 * decoded byte FONT1.FNT; req is decoded frame zero of "req". Render uses
 * allocation bounds for safe padded writes. Errors preserve canvas and RNG.
 * Clock/input/answer changes remain in menu_modal.c.
 */
enum mh_error_code	mh_menu_dialog_render(
	const struct mh_menu_dialog_text *text, const struct mh_font *font,
	const struct mh_sprite_indexed *req,
	const unsigned char noise[MH_MENU_NOISE_BYTES], float x,
	unsigned int selected, unsigned long *random,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
