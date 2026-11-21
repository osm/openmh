#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_windows_path.h"

enum mh_error_code
mh_filesystem_executable_directory(
	char *output, size_t capacity, struct mh_error *error_out)
{
	WCHAR *wide;
	char *path, *slash;
	DWORD length;
	size_t size;
	enum mh_error_code status;

	if (output == NULL || capacity == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing executable directory output");

	wide = malloc(MH_WINDOWS_PATH_CHARACTERS * sizeof(*wide));

	if (wide == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory locating executable");

	length = GetModuleFileNameW(NULL, wide, MH_WINDOWS_PATH_CHARACTERS);

	if (length == 0)
		status = mh_windows_file_error(GetLastError(),
			"cannot locate the running executable", error_out);
	else if (length >= MH_WINDOWS_PATH_CHARACTERS)
		status = mh_error_set(error_out, MH_ERROR_RANGE,
			"executable path is too long");
	else
		status = mh_windows_path_to_utf8(wide, &path, error_out);

	free(wide);

	if (status != MH_ERROR_NONE)
		return status;

	slash = strrchr(path, '/');

	if (slash == NULL) {
		free(path);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"executable path has no directory");
	}

	size = (size_t)(slash - path);

	/* Preserve a drive root, C:/, instead of the drive-relative path C:. */
	if (size > 0 && path[size - 1] == ':')
		size++;

	if (size >= capacity) {
		free(path);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"executable directory is too long");
	}

	memcpy(output, path, size);
	output[size] = 0;
	free(path);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
