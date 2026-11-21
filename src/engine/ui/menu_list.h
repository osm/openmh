#ifndef MH_MENU_LIST_H
#define MH_MENU_LIST_H

#include "engine/core/error.h"
#include "engine/ui/menu_text.h"
#include "interfaces/sprite/pixels.h"

/*
 * A vertical filename list, 15-pixel rows, centered on the selected index
 * without bottom-edge backfilling. active chooses palette entry 2 instead of 1;
 * selected text is 5 over a width+8 by 12 rectangle. Optional marker is the
 * already-selected decoded frame of sprite "mupp". Names are bounded byte
 * strings (1..255 bytes), count <=4096, visible <=256. All pixels remain
 * unchanged on failure.
 */
enum mh_error_code	mh_menu_list_draw(const struct mh_font *font,
	const char *const *names, unsigned int count, unsigned int selected,
	unsigned int visible, int active,
	const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * List renderer with a retained low-16-bit selection that may be outside the
 * label list. The visible window still follows that selection; no row is
 * highlighted when the selected label is absent. Rankings has three category
 * labels but retains a category-3 drawing branch.
 */
enum mh_error_code	mh_menu_list_selection_draw(const struct mh_font *font,
	const char *const *names, unsigned int count, unsigned int selected,
	unsigned int visible, int active,
	const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * Address/session record names allow empty strings. Focus uses palette 2
 * instead of 1 and changes the marker frame supplied by the caller. Selected
 * text is 5. No native record casts.
 */
enum mh_error_code	mh_menu_record_list_draw(const struct mh_font *font,
	const char *const *names, unsigned int count, unsigned int selected,
	unsigned int visible, int active,
	const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * Horizontal choices. Same centered selection window and colors, with each
 * displayed name advancing by its text width plus 16 pixels. No backfilling at
 * the final item. Failure preserves the complete canvas.
 */
enum mh_error_code	mh_menu_choices_draw(const struct mh_font *font,
	const char *const *names, unsigned int count, unsigned int selected,
	unsigned int visible, int active, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
