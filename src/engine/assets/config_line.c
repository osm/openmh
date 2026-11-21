#include <string.h>

#include "engine/assets/config_line.h"

enum mh_error_code
mh_config_line_read(const unsigned char *text, size_t size, size_t *position,
	char *line, size_t capacity, unsigned int max_byte, int trim)
{
	size_t length;
	char *comment;

	length = 0;

	while (*position < size && text[*position] != '\r' &&
		text[*position] != '\n') {
		if ((text[*position] < 32 && text[*position] != '\t') ||
			text[*position] > max_byte)
			return MH_ERROR_UNSUPPORTED;

		if (length == capacity - 1)
			return MH_ERROR_RANGE;

		line[length++] = (char)text[(*position)++];
	}

	if (*position < size && text[*position] == '\r')
		(*position)++;

	if (*position < size && text[*position] == '\n')
		(*position)++;

	line[length] = 0;
	comment = strstr(line, "//");

	if (comment != NULL)
		*comment = 0;

	if (trim) {
		length = strlen(line);

		while (length != 0 &&
			(line[length - 1] == ' ' || line[length - 1] == '\t'))
			line[--length] = 0;
	}

	return MH_ERROR_NONE;
}
