#ifndef MH_MENU_RECORDING_DRAW_H
#define MH_MENU_RECORDING_DRAW_H

#include "engine/core/error.h"
#include "engine/ui/menu_recording.h"
#include "engine/ui/menu_text.h"

/*
 * Original FONT1 labels at (64,200)/(64,220), palette colors 1/2;
 * cursor uses the post-edit width and 200ms clock /100ms duty cycle.
 */
enum mh_error_code	mh_menu_recording_render(
	const struct mh_menu_recording_output *output,
	const struct mh_font *font, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
