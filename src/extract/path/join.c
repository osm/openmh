#include <stdlib.h>
#include <string.h>

#include "extract/path/join.h"

enum mh_error_code
mh_extract_join_prefix(const char *root, size_t root_length,
	const char *relative, char **output, struct mh_error *error_out)
{
	char *path;
	size_t length, offset;

	if (root == NULL || relative == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing extraction path");

	length = strlen(relative);

	if (length > (size_t)-1 - 2 || root_length > (size_t)-1 - length - 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"extraction path is too long");

	path = malloc(root_length + length + 2);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory joining extraction path");

	memcpy(path, root, root_length);
	offset = root_length;

	if (offset != 0 && path[offset - 1] != '/' && path[offset - 1] != '\\')
		path[offset++] = '/';

	memcpy(path + offset, relative, length + 1);
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_join(const char *root, const char *relative, char **output,
	struct mh_error *error_out)
{
	return mh_extract_join_prefix(root, root == NULL ? 0 : strlen(root),
		relative, output, error_out);
}
