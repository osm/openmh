#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_provider.h"

enum mh_error_code
mh_filesystem_executable_directory(
	char *output, size_t capacity, struct mh_error *error_out)
{
	char path[PATH_MAX], *slash;
	ssize_t length;
	size_t size;

	if (output == NULL || capacity == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing executable directory output");

	length = readlink("/proc/self/exe", path, sizeof(path) - 1);

	if (length < 0)
		return mh_error_system(error_out, errno,
			"cannot locate the running executable");

	if ((size_t)length >= sizeof(path) - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"executable path is too long");

	path[length] = 0;
	slash = strrchr(path, '/');

	if (slash == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"executable path has no directory");

	size = slash == path ? 1 : (size_t)(slash - path);

	if (size >= capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"executable directory is too long");

	memcpy(output, path, size);
	output[size] = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
