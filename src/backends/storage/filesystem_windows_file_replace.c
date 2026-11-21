#include <stdlib.h>

#include "backends/storage/filesystem_file_replace.h"
#include "backends/storage/filesystem_windows_temporary.h"

static enum mh_error_code
resolve_destination(const char *path, HANDLE *directory, WCHAR **output,
	struct mh_error *error_out)
{
	WCHAR *wide, *full, *name, *destination;
	HANDLE handle;
	char *basename;
	DWORD length;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	full = malloc(MH_WINDOWS_PATH_CHARACTERS * sizeof(*full));

	if (full == NULL) {
		free(wide);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resolving saved file");
	}

	length =
		GetFullPathNameW(wide, MH_WINDOWS_PATH_CHARACTERS, full, &name);
	free(wide);

	if (length == 0 || length >= MH_WINDOWS_PATH_CHARACTERS ||
		name == NULL || *name == 0) {
		free(full);
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid saved file path");
	}

	basename = NULL;
	status = mh_windows_path_to_utf8(name, &basename, error_out);
	*name = 0;

	if (status == MH_ERROR_NONE)
		status = mh_windows_open_directory(full,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &handle, error_out);

	free(full);

	if (status != MH_ERROR_NONE) {
		free(basename);
		return status;
	}

	status = mh_windows_path_to_wide(basename, &destination, error_out);

	free(basename);

	if (status != MH_ERROR_NONE) {
		CloseHandle(handle);
		return status;
	}

	*directory = handle;
	*output = destination;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_file_replace_status
mh_file_replace(const char *path, const void *bytes, size_t size)
{
	struct mh_windows_temporary temporary;
	WCHAR *destination;
	HANDLE directory;
	enum mh_error_code status;

	directory = INVALID_HANDLE_VALUE;
	destination = NULL;
	status = resolve_destination(path, &directory, &destination, NULL);

	if (status != MH_ERROR_NONE)
		return status == MH_ERROR_MEMORY ? MH_FILE_REPLACE_MEMORY
						: MH_FILE_REPLACE_PATH;

	status = mh_windows_temporary_open(directory, &temporary, NULL);

	if (status != MH_ERROR_NONE) {
		free(destination);
		CloseHandle(directory);
		return status == MH_ERROR_MEMORY ? MH_FILE_REPLACE_MEMORY
						: MH_FILE_REPLACE_CREATE;
	}

	status = mh_windows_write_bytes(temporary.handle, bytes, size, NULL);

	if (status == MH_ERROR_NONE)
		status = mh_windows_temporary_seal(&temporary, size, NULL);

	if (status == MH_ERROR_NONE)
		status = mh_windows_temporary_publish(
			&temporary, directory, destination, 1, NULL);

	mh_windows_temporary_close(&temporary);
	free(destination);
	CloseHandle(directory);

	return status == MH_ERROR_NONE ? MH_FILE_REPLACE_OK
				: MH_FILE_REPLACE_COMMIT;
}
