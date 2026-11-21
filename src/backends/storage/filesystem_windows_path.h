#ifndef MH_FILESYSTEM_WINDOWS_PATH_H
#define MH_FILESYSTEM_WINDOWS_PATH_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "engine/core/error.h"

/*
 * Windows paths are UTF-8 at the backend boundary, UTF-16 inside this module.
 */
#define MH_WINDOWS_PATH_CHARACTERS 32768

/* Owned outputs, unchanged on failure. Both functions normalize separators. */
enum mh_error_code	mh_windows_path_to_wide(const char *path,
	WCHAR **output, struct mh_error *error_out);
enum mh_error_code	mh_windows_path_to_utf8(const WCHAR *path,
	char **output, struct mh_error *error_out);

/* Classify a captured Win32 error; messages remain borrowed literals. */
enum mh_error_code	mh_windows_file_error(DWORD number, const char *message,
	struct mh_error *error_out);

#endif
