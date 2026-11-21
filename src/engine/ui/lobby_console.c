#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/lobby_console.h"

static int
valid_scroll(const struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages)
{
	return scroll != NULL && messages != NULL &&
		scroll->offset <= MH_LOBBY_SCROLL_MAX &&
		scroll->revision <= MH_U32_MAX &&
		messages->revision <= MH_U32_MAX;
}

enum mh_error_code
mh_lobby_console_scroll(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char events[MH_KEY_COUNT], struct mh_error *error_out)
{
	struct mh_lobby_scroll next;

	if (!valid_scroll(scroll, messages) || events == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby scroll");

	next = *scroll;

	if (events[MH_KEY_PAGEUP] && next.offset < MH_LOBBY_SCROLL_MAX &&
		messages->rows[next.offset + MH_MESSAGE_VISIBLE_ROWS].text[0]) {
		next.offset++;
		next.revision = messages->revision;
	}

	if (events[MH_KEY_PAGEDOWN] && next.offset != 0)
		next.offset--;

	if (events[MH_KEY_HOME]) {
		next.offset = 0;

		while (next.offset < MH_LOBBY_SCROLL_MAX &&
			messages->rows[next.offset + MH_MESSAGE_VISIBLE_ROWS]
				.text[0])
			next.offset++;
	}

	if (events[MH_KEY_END])
		next.offset = 0;

	*scroll = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
palette_index(const unsigned char *palette, unsigned long rgb)
{
	unsigned int i;

	for (i = 0; i < 255; i++)
		if (palette[i * 3] == ((rgb >> 16) & 255) &&
			palette[i * 3 + 1] == ((rgb >> 8) & 255) &&
			palette[i * 3 + 2] == (rgb & 255))
			return i;

	return 0;
}

static void
append_line(struct mh_lobby_console_view *view, const char *text,
	unsigned int x, unsigned int y, unsigned int index)
{
	struct mh_lobby_console_line *line;

	line = &view->lines[view->count++];
	strcpy((char *)line->text, text);
	line->x = x;
	line->y = y;
	line->color = index;
}

enum mh_error_code
mh_lobby_console_view(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char text[MH_MESSAGE_BYTES],
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_lobby_console_view *view,
	struct mh_error *error_out)
{
	static const char marker[] =
		"^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^";
	struct mh_lobby_scroll next;
	struct mh_lobby_console_view result;
	const struct mh_message *message;
	unsigned int i, first, width, orange;
	unsigned long offset;

	if (!valid_scroll(scroll, messages) || text == NULL ||
		advance == NULL || palette == NULL || view == NULL ||
		milliseconds > MH_U32_MAX ||
		memchr(text, 0, MH_MESSAGE_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby console view");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby font width");

	next = *scroll;

	if (next.offset != 0) {
		offset = (next.offset +
				((messages->revision - next.revision) &
					MH_U32_MAX)) &
			MH_U32_MAX;
		next.offset = offset > MH_LOBBY_SCROLL_MAX
			? MH_LOBBY_SCROLL_MAX
			: (unsigned int)offset;
		next.revision = messages->revision;
	}

	memset(&result, 0, sizeof(result));

	for (i = 0; i < MH_MESSAGE_VISIBLE_ROWS; i++) {
		message = &messages->rows[next.offset +
			MH_MESSAGE_VISIBLE_ROWS - 1 - i];

		if (memchr(message->text, 0, MH_MESSAGE_BYTES) == NULL ||
			message->color > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby message");

		append_line(&result, message->text, 50, 300 + i * 12,
			palette_index(palette, message->color));
	}

	orange = palette_index(palette, 0xe06820UL);

	if (next.offset)
		append_line(&result, marker, 50, 358, orange);

	first = (unsigned int)strlen((const char *)text);
	width = 0;

	while (first != 0 &&
		width + advance[text[first - 1]] <= MH_LOBBY_CONSOLE_WIDTH) {
		width += advance[text[first - 1]];
		first--;
	}

	append_line(&result, (const char *)text + first, 50, 370, orange);

	if (milliseconds % 1000 >= 500)
		append_line(&result, "|", 50 + width, 370, orange);

	*scroll = next;
	*view = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
draw_view(const struct mh_lobby_console_view *view, const struct mh_font *font,
	struct mh_menu_canvas *canvas)
{
	unsigned int i;

	/* Both callers preflight the complete font, canvas and bounded view. */

	for (i = 0; i < view->count; i++)
		mh_menu_text_draw(font, view->lines[i].text, MH_MESSAGE_BYTES,
			view->lines[i].x, view->lines[i].y,
			view->lines[i].color, canvas, NULL);
}

enum mh_error_code
mh_lobby_console_draw(struct mh_lobby_scroll *scroll,
	const struct mh_messages *messages,
	const unsigned char text[MH_MESSAGE_BYTES], const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_lobby_scroll next;
	struct mh_lobby_console_view view;
	enum mh_error_code status;

	/*
	 * The empty draw validates all font/canvas fields without writing. The
	 * generated commands below have bounded text, color and coordinates.
	 */
	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, 0, 0, 0, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scroll == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby scroll");

	next = *scroll;
	status = mh_lobby_console_view(&next, messages, text, font->advance,
		palette, milliseconds, &view, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	draw_view(&view, font, canvas);

	*scroll = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_password_view(const unsigned char text[16],
	const unsigned int advance[MH_FONT_GLYPHS],
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_lobby_console_view *view,
	struct mh_error *error_out)
{
	struct mh_lobby_console_view result;
	unsigned int i, width, orange;

	if (text == NULL || advance == NULL || palette == NULL ||
		view == NULL || milliseconds > MH_U32_MAX ||
		memchr(text, 0, 16) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby password view");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby font width");

	memset(&result, 0, sizeof(result));
	orange = palette_index(palette, 0xe06820UL);
	append_line(
		&result, "This session require a password!", 150, 310, orange);
	append_line(&result, "Type password:", 150, 330, orange);
	append_line(&result, (const char *)text, 290, 330, orange);
	width = 0;

	for (i = 0; text[i]; i++)
		width += advance[text[i]];

	if (milliseconds % 1000 >= 500)
		append_line(&result, "|", 290 + width, 330, orange);

	*view = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_password_draw(const unsigned char text[16], const struct mh_font *font,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned long milliseconds, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_lobby_console_view view;
	enum mh_error_code status;

	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, 0, 0, 0, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_lobby_password_view(
		text, font->advance, palette, milliseconds, &view, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	draw_view(&view, font, canvas);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
