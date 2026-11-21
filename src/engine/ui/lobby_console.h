#ifndef MH_UI_LOBBY_CONSOLE_H
#define MH_UI_LOBBY_CONSOLE_H

#include "engine/core/error.h"
#include "engine/input/keyboard.h"
#include "engine/ui/menu_text.h"
#include "engine/ui/messages.h"
#include "interfaces/image/pixels.h"

#define MH_LOBBY_CONSOLE_LINES 8
#define MH_LOBBY_CONSOLE_WIDTH 540
#define MH_LOBBY_SCROLL_MAX (MH_MESSAGE_ROWS - MH_MESSAGE_VISIBLE_ROWS)

struct mh_lobby_scroll {
	/* Native bounded to the oldest five rows. */
	unsigned int	offset;

	/* Last observed notification revision. */
	unsigned long	revision;
};

struct mh_lobby_console_line {
	unsigned char	text[MH_MESSAGE_BYTES];

	/* Indexed palette color, not packed RGB. */
	unsigned int	x, y, color;
};

struct mh_lobby_console_view {
	unsigned int			count;
	struct mh_lobby_console_line	lines[MH_LOBBY_CONSOLE_LINES];
};

/*
 * PageUp, PageDown, Home, End. Home stops at the first empty row after row 4;
 * only a successful PageUp updates revision. Run after command dispatch so new
 * command messages can be scrolled.
 */
enum mh_error_code	mh_lobby_console_scroll(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char events[MH_KEY_COUNT], struct mh_error *error_out);

/*
 * Anchor scrolled messages as notifications arrive, then draw five rows, the
 * optional scroll marker, the <=540px draft suffix and a cursor when
 * milliseconds%1000>=500. FONT1 supplies the advances. Match the first exact
 * RGB palette entry 0..254, else index 0. Palette is 256 RGB triples. Clamp an
 * expired scroll anchor at 95 to keep message access in bounds. Inputs/outputs
 * unchanged on error. Render precedes this frame's input.
 */
enum mh_error_code	mh_lobby_console_view(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char text[MH_MESSAGE_BYTES],
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_lobby_console_view *view,
	struct mh_error *error_out);

/*
 * Password admission replaces the console, with the original visible text (no
 * masking) and identical cursor timing.
 */
enum mh_error_code	mh_lobby_password_view(const unsigned char text[16],
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_lobby_console_view *view,
	struct mh_error *error_out);

/*
 * Compose the above view with the decoded original FONT1 byte programs.
 * Background/roster belong to the caller; this only paints console glyphs.
 * Errors preserve both pixels and scroll state.
 */
enum mh_error_code	mh_lobby_console_draw(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char text[MH_MESSAGE_BYTES], const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);
enum mh_error_code	mh_lobby_password_draw(const unsigned char text[16],
	const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_menu_canvas *canvas,
	struct mh_error *error_out);

#endif
