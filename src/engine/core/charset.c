#include <stdlib.h>
#include <string.h>

#include "engine/core/charset.h"

unsigned char
mh_windows_1252_character(unsigned long character)
{
	static const unsigned long punctuation[] = {0x20ac, 0, 0x201a, 0x0192,
		0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039,
		0x0152, 0, 0x017d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
		0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,
		0x017e, 0x0178};

	unsigned int i;

	if (character < 128 || (character >= 160 && character <= 255))
		return (unsigned char)character;

	for (i = 0; i < sizeof(punctuation) / sizeof(punctuation[0]); i++)
		if (character == punctuation[i])
			return (unsigned char)(128 + i);

	return 0;
}

static unsigned long
utf8_character(const unsigned char **cursor, const unsigned char *end)
{
	const unsigned char *start;
	unsigned long character, minimum;
	unsigned int length, i;

	start = *cursor;
	*cursor = start + 1;

	if (*start < 0x80)
		return *start;

	if (*start >= 0xc2 && *start <= 0xdf) {
		length = 2;
		minimum = 0x80;
		character = *start & 0x1f;
	} else if (*start >= 0xe0 && *start <= 0xef) {
		length = 3;
		minimum = 0x800;
		character = *start & 0x0f;
	} else if (*start >= 0xf0 && *start <= 0xf4) {
		length = 4;
		minimum = 0x10000UL;
		character = *start & 7;
	} else
		return 0;

	if ((size_t)(end - start) < length)
		return 0;

	for (i = 1; i < length; i++) {
		if ((start[i] & 0xc0) != 0x80)
			return 0;

		character = (character << 6) | (start[i] & 0x3f);
	}

	if (character < minimum || character > 0x10ffffUL ||
		(character >= 0xd800 && character <= 0xdfff))
		return 0;

	*cursor = start + length;

	return character;
}

enum mh_error_code
mh_utf8_to_windows_1252(
	const char *text, char **output, struct mh_error *error_out)
{
	const unsigned char *cursor, *end;
	char *converted, *next;
	unsigned char character;
	size_t length;

	if (text == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing text conversion arguments");

	length = strlen(text);
	cursor = (const unsigned char *)text;
	end = cursor + length;
	converted = malloc(length + 1);

	if (converted == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting text");

	next = converted;

	while (cursor < end) {
		character =
			mh_windows_1252_character(utf8_character(&cursor, end));

		if (character != 0)
			*next++ = (char)character;
	}

	*next = 0;
	*output = converted;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
