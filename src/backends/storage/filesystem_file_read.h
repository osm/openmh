#ifndef MH_BACKENDS_STORAGE_FILESYSTEM_FILE_READ_H
#define MH_BACKENDS_STORAGE_FILESYSTEM_FILE_READ_H

#include "engine/core/error.h"
#include "engine/core/file.h"

/*
 * Native diagnostic convenience; game resources use a selected provider.
 * Caller frees data with free(). Output is assigned only on success.
 * Tools cap individual files at 256 MiB to bound resource use.
 */
enum mh_error_code	mh_file_read(const char *path, struct mh_file *file,
	struct mh_error *error_out);

#endif
