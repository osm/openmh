#ifndef MH_FILESYSTEM_WINDOWS_STAGING_CLEANUP_H
#define MH_FILESYSTEM_WINDOWS_STAGING_CLEANUP_H

#include "backends/storage/filesystem_windows_handle.h"

/* Owned private/retired tree only. Never follows reparse points. */
enum mh_error_code	mh_windows_staging_remove(HANDLE directory,
	struct mh_error *error_out);

#endif
