#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/text_edit.h"

static unsigned long
text_width(const unsigned char *text,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS])
{
	unsigned long width;

	width = 0;

	while (*text != 0)
		width += advance[*text++];

	return width;
}

enum mh_error_code
mh_text_edit(unsigned char *text, size_t capacity, unsigned int limit,
	unsigned long max_width, const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_text_edit_output *output, struct mh_error *error_out)
{
	struct mh_text_edit_output out;
	unsigned char next[MH_TEXT_EDIT_BYTES];
	size_t length, previous_length;
	unsigned int i;

	if (text == NULL || events == NULL || characters == NULL ||
		advance == NULL || output == NULL || capacity == 0 ||
		capacity > sizeof(next) || limit == 0 || limit > capacity ||
		max_width > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid text editing context");

	for (i = 0; i < MH_TEXT_EDIT_CHARACTERS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid text editing metric");

	length = 0;

	while (length < capacity && text[length] != 0)
		length++;

	if (length == capacity)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated editable text");

	memcpy(next, text, capacity);
	memset(&out, 0, sizeof(out));
	out.last_key = MH_U32_MAX;

	for (i = 0; i < MH_KEY_COUNT; i++)
		if (events[i] == 1) {
			previous_length = length;
			out.last_key = i;

			if (characters[i] >= ' ') {
				if (length < limit - 1) {
					next[length++] = characters[i];
					next[length] = 0;
					out.actions[out.count++] =
						MH_TEXT_EDIT_ACCEPT;
				} else
					out.actions[out.count++] =
						MH_TEXT_EDIT_REJECT;
			}

			if (i == MH_KEY_BACKSPACE) {
				if (previous_length != 0) {
					length = previous_length - 1;
					next[length] = 0;
					out.actions[out.count++] =
						MH_TEXT_EDIT_ACCEPT;
				} else
					out.actions[out.count++] =
						MH_TEXT_EDIT_REJECT;
			}

			while (text_width(next, advance) > max_width) {
				next[--length] = 0;
				out.actions[out.count++] = MH_TEXT_EDIT_REJECT;
			}
		}

	out.width = text_width(next, advance);
	memcpy(text, next, capacity);
	*output = out;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
