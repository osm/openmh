#include <string.h>

#include "engine/archive/path.h"

static int
valid_path(const char *path)
{
	const char *start, *scan;
	size_t size;

	start = path;

	for (scan = path;; scan++) {
		if (*scan == ':' || (unsigned char)*scan >= 127 ||
			(*scan != '\0' && (unsigned char)*scan < 32))
			return 0;

		if (*scan != '/' && *scan != '\0')
			continue;

		size = (size_t)(scan - start);

		if (size == 0 || (size == 1 && start[0] == '.') ||
			(size == 2 && start[0] == '.' && start[1] == '.'))
			return 0;

		if (*scan == '\0')
			return 1;

		start = scan + 1;
	}
}

enum mh_error_code
mh_archive_path(const char *directory, const char *name,
	char output[MH_ARCHIVE_PATH_SIZE], struct mh_error *error_out)
{
	char path[MH_ARCHIVE_PATH_SIZE];
	size_t prefix, length, i;

	if (directory == NULL || name == NULL || output == NULL ||
		*name == '\0' || strchr(name, '/') != NULL ||
		strchr(name, '\\') != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid archive member name");

	prefix = strlen(directory);
	length = strlen(name);

	if (prefix >= sizeof(path) - 1 || length >= sizeof(path) - prefix - 1)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "archive path is too long");

	memcpy(path, directory, prefix);

	if (prefix != 0)
		path[prefix++] = '/';

	memcpy(path + prefix, name, length + 1);

	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == '\\')
			path[i] = '/';
	}

	if (!valid_path(path))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unsafe archive path");

	memcpy(output, path, prefix + length + 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
