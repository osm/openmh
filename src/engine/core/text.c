#include <string.h>

#include "engine/core/text.h"

enum mh_error_code
mh_text_copy(char *destination, size_t capacity, const char *source,
	struct mh_error *error_out)
{
	size_t length;

	if (destination == NULL || source == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing text buffer");

	for (length = 0; length < capacity; length++) {
		if (source[length] == 0) {
			memmove(destination, source, length + 1);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "text exceeds buffer capacity");
}

enum mh_error_code
mh_text_append(char *destination, size_t capacity, const char *source,
	struct mh_error *error_out)
{
	size_t length;

	if (destination == NULL || source == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing text buffer");

	for (length = 0; length < capacity; length++)
		if (destination[length] == 0)
			return mh_text_copy(destination + length,
				capacity - length, source, error_out);

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "unterminated text buffer");
}

static unsigned int
ascii_lower(unsigned char byte)
{
	if (byte >= 'A' && byte <= 'Z')
		return byte + ('a' - 'A');

	return byte;
}

void
mh_ascii_lower(char *text)
{
	for (; *text != 0; text++)
		*text = (char)ascii_lower((unsigned char)*text);
}

int
mh_ascii_compare(const char *left, const char *right)
{
	unsigned int a, b;

	do {
		a = ascii_lower((unsigned char)*left++);
		b = ascii_lower((unsigned char)*right++);

		if (a != b)
			return a < b ? -1 : 1;
	} while (a != 0);

	return 0;
}

int
mh_ascii_equal(const char *left, const char *right)
{
	return mh_ascii_compare(left, right) == 0;
}

int
mh_ascii_equal_n(const char *left, const char *right, size_t limit)
{
	size_t i;
	unsigned int a, b;

	for (i = 0; i < limit; i++) {
		a = ascii_lower((unsigned char)left[i]);
		b = ascii_lower((unsigned char)right[i]);

		if (a != b)
			return 0;

		if (a == 0)
			return 1;
	}

	return 1;
}

int
mh_text_field_valid(const char *text, size_t capacity)
{
	size_t i;

	if (text == NULL)
		return 0;

	for (i = 0; i < capacity; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

int
mh_text_display_byte(unsigned char byte)
{
	enum {
		DELETE_BYTE = 0x7f,
		C1_LAST = 0x9f
	};

	return byte >= ' ' && (byte < DELETE_BYTE || byte > C1_LAST);
}

int
mh_text_display_valid(const char *text, size_t capacity)
{
	size_t i;
	unsigned char byte;

	if (text == NULL)
		return 0;

	for (i = 0; i < capacity; i++) {
		byte = (unsigned char)text[i];

		if (byte == 0)
			return 1;

		if (!mh_text_display_byte(byte))
			return 0;
	}

	return 0;
}
