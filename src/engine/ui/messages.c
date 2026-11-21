#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/messages.h"

#define DEFAULT_WIDTH 320
#define VIEW_HEIGHT_MIN 200
#define VIEW_HEIGHT_MAX 16384
#define FONT_HEIGHT_MAX 256
#define TEXT_LEFT 5
#define TEXT_BOTTOM_ROWS 7
#define MESSAGE_LIFETIME_MS 5000
#define SOUND_LIFETIME_MS 500
#define EXACT_MILLISECONDS_MAX 9007199254740991.0

void
mh_messages_init(struct mh_messages *messages)
{
	if (messages == NULL)
		return;

	memset(messages, 0, sizeof(*messages));
	messages->width = DEFAULT_WIDTH;
}

void
mh_messages_clear(struct mh_messages *messages)
{
	unsigned int i;

	if (messages == NULL)
		return;

	for (i = 0; i < MH_MESSAGE_ROWS; i++) {
		messages->rows[i].text[0] = 0;
		messages->rows[i].milliseconds = 0;
	}
}

enum mh_error_code
mh_messages_width(struct mh_messages *messages, unsigned long width,
	struct mh_error *error_out)
{
	if (messages == NULL || width > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid message width");

	if (width != 0)
		messages->width = width;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_history(const struct mh_messages *messages)
{
	unsigned int i;

	if (messages == NULL || messages->width > MH_U32_MAX ||
		messages->revision > MH_U32_MAX)
		return 0;

	for (i = 0; i < MH_MESSAGE_ROWS; i++)
		if (memchr(messages->rows[i].text, 0, MH_MESSAGE_BYTES) ==
				NULL ||
			messages->rows[i].color > MH_U32_MAX ||
			messages->rows[i].milliseconds > MH_U32_MAX)
			return 0;

	return 1;
}

static void
shift_history(struct mh_messages *messages)
{
	unsigned int i;

	for (i = MH_MESSAGE_ROWS - 1; i != 0; i--) {
		/*
		 * Original strcpy leaves destination bytes after the NUL
		 * untouched.
		 */
		strcpy(messages->rows[i].text, messages->rows[i - 1].text);
		messages->rows[i].color = messages->rows[i - 1].color;
		messages->rows[i].milliseconds =
			messages->rows[i - 1].milliseconds;
	}
}

static void
write_newest(struct mh_messages *messages, const char *text,
	unsigned int length, unsigned long color, unsigned long milliseconds)
{
	memcpy(messages->rows[0].text, text, length);

	messages->rows[0].text[length] = 0;
	messages->rows[0].color = color;
	messages->rows[0].milliseconds = milliseconds;
}

static unsigned int
join_text(const char *text, const char *suffix, char joined[MH_MESSAGE_BYTES])
{
	unsigned int length;

	length = 0;

	while (length < MH_MESSAGE_BYTES - 1 && *text != 0)
		joined[length++] = *text++;

	if (suffix != NULL)
		while (length < MH_MESSAGE_BYTES - 1 && *suffix != 0)
			joined[length++] = *suffix++;

	joined[length] = 0;

	return length;
}

enum mh_error_code
mh_messages_add(struct mh_messages *messages, const char *text,
	const char *suffix, unsigned long color, unsigned long milliseconds,
	const unsigned int advance[MH_FONT_GLYPHS], struct mh_error *error_out)
{
	char joined[MH_MESSAGE_BYTES];
	unsigned int i, length, start;
	unsigned long width;

	if (!valid_history(messages) || text == NULL || advance == NULL ||
		color > MH_U32_MAX || milliseconds > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid message history/input");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid message glyph advance");

	length = join_text(text, suffix, joined);
	shift_history(messages);
	start = 0;
	width = 0;

	for (i = 0; i < length; i++) {
		width += advance[(unsigned char)joined[i]];

		if (width > messages->width) {
			write_newest(messages, joined + start, i - start, color,
				milliseconds);
			shift_history(messages);

			/* Increment once for each of the 99 copied rows. */
			messages->revision =
				(messages->revision + (MH_MESSAGE_ROWS - 1)) &
				MH_U32_MAX;
			start = i;
			width = 0;
		}
	}

	write_newest(
		messages, joined + start, length - start, color, milliseconds);
	messages->revision = (messages->revision + 1) & MH_U32_MAX;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_messages_view(const struct mh_messages *messages, double milliseconds,
	unsigned int height, unsigned int font_height,
	struct mh_message_view *view, struct mh_error *error_out)
{
	struct mh_message_view next;
	unsigned int i, row;

	if (!valid_history(messages) || view == NULL ||
		!(milliseconds >= 0 &&
			milliseconds <= EXACT_MILLISECONDS_MAX) ||
		height < VIEW_HEIGHT_MIN || height > VIEW_HEIGHT_MAX ||
		font_height > FONT_HEIGHT_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid message view");

	memset(&next, 0, sizeof(next));

	for (i = 0; i < MH_MESSAGE_VISIBLE_ROWS; i++) {
		row = MH_MESSAGE_VISIBLE_ROWS - 1 - i;

		if (milliseconds < (double)((messages->rows[row].milliseconds +
						MESSAGE_LIFETIME_MS) &
					MH_U32_MAX)) {
			next.lines[next.count].row = row;
			next.lines[next.count].x = TEXT_LEFT;
			next.lines[next.count].y = (int)height -
				(TEXT_BOTTOM_ROWS - (int)i) * (int)font_height;
			next.count++;
		}
	}

	next.sound = milliseconds <
		(double)((messages->rows[0].milliseconds + SOUND_LIFETIME_MS) &
			MH_U32_MAX);
	*view = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
