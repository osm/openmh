#ifndef MH_FILESYSTEM_FILE_REPLACE_H
#define MH_FILESYSTEM_FILE_REPLACE_H

#include <stddef.h>

/* Status order also indexes the callers' file-specific diagnostics. */
enum mh_file_replace_status {
	MH_FILE_REPLACE_OK,
	MH_FILE_REPLACE_PATH,
	MH_FILE_REPLACE_MEMORY,
	MH_FILE_REPLACE_CREATE,
	MH_FILE_REPLACE_STREAM,
	MH_FILE_REPLACE_COMMIT
};

/*
 * Private filesystem operation for already validated paths and encoded bytes.
 * Write and flush/sync a sibling temporary, then replace the destination.
 * Windows retains a metadata handle through publication after closing the
 * writer. The caller retains path/data and maps failures to its own diagnostic.
 */
enum mh_file_replace_status	mh_file_replace(const char *path,
	const void *data, size_t size);

#endif
