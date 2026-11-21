#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/scene_text.h"

#define VALUE_LIMIT 1e6

/* 1 = nonempty line, 0 = EOF, -1 = malformed text. */
int
mh_scene_text_next(struct mh_scene_text *input)
{
	size_t length;
	size_t start;
	unsigned char character;

	while (input->cursor < input->size) {
		length = 0;

		while (input->cursor < input->size) {
			character = input->text[input->cursor++];

			if (character == '\r' || character == '\n')
				break;

			if ((character < 32 && character != '\t') ||
				character > 126 ||
				length == sizeof(input->line) - 1) {
				return -1;
			}

			input->line[length++] = (char)character;
		}

		while (length != 0 &&
			(input->line[length - 1] == ' ' ||
				input->line[length - 1] == '\t'))
			length--;

		start = 0;

		while (start < length &&
			(input->line[start] == ' ' ||
				input->line[start] == '\t'))
			start++;

		memmove(input->line, input->line + start, length - start);
		input->line[length - start] = 0;

		if (length != start)
			return 1;
	}

	return 0;
}

int
mh_scene_text_numbers(const char *text, double *values, unsigned int count)
{
	unsigned int i;
	char *end;

	for (i = 0; i < count; i++) {
		errno = 0;
		values[i] = strtod(text, &end);

		if (end == text || errno != 0 ||
			!(values[i] >= -VALUE_LIMIT &&
				values[i] <= VALUE_LIMIT))
			return 0;

		if (i + 1 < count && *end != ' ' && *end != '\t')
			return 0;

		text = end;
	}

	while (*text == ' ' || *text == '\t')
		text++;

	return *text == 0;
}
