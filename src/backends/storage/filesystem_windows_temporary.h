#ifndef MH_FILESYSTEM_WINDOWS_TEMPORARY_H
#define MH_FILESYSTEM_WINDOWS_TEMPORARY_H

#include "backends/storage/filesystem_windows_handle.h"

/*
 * Owns the file handle, not its directory. Closing removes unpublished bytes.
 */
struct mh_windows_temporary {
	HANDLE				handle;
	BY_HANDLE_FILE_INFORMATION	identity;
};

enum mh_error_code	mh_windows_temporary_open(HANDLE directory,
	struct mh_windows_temporary *output, struct mh_error *error_out);
enum mh_error_code	mh_windows_temporary_seal(
	struct mh_windows_temporary *temporary, size_t size,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_temporary_publish(
	struct mh_windows_temporary *temporary, HANDLE directory,
	const WCHAR *destination, int replace, struct mh_error *error_out);
void			mh_windows_temporary_close(
	struct mh_windows_temporary *temporary);

#endif
