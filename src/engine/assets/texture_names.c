#include <stdlib.h>
#include <string.h>

#include "engine/assets/texture_names.h"

enum mh_error_code
mh_texture_names_copy(const unsigned char *data, size_t size, size_t count,
	char **names, const char *memory_error, struct mh_error *error_out)
{
	const unsigned char *end;
	size_t i, length;

	for (i = 0; i < count; i++) {
		end = (const unsigned char *)memchr(data, 0, size);

		if (end == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated texture name");

		length = (size_t)(end - data) + 1;
		names[i] = (char *)malloc(length);

		if (names[i] == NULL)
			return mh_error_set(
				error_out, MH_ERROR_MEMORY, memory_error);

		memcpy(names[i], data, length);
		data += length;
		size -= length;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
