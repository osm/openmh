#ifndef MH_FILESYSTEM_PLATFORM_H
#define MH_FILESYSTEM_PLATFORM_H

#include "backends/storage/filesystem_stream.h"
#include "engine/core/error.h"
#include "interfaces/storage/directory.h"

/* Private directory operations; names use the host's filesystem encoding. */
enum mh_error_code	mh_filesystem_list(void *context, const char *path,
	mh_directory_visit visit, void *visitor, struct mh_error *error_out);
enum mh_error_code	mh_filesystem_create_directory(void *context,
	const char *path, struct mh_error *error_out);
enum mh_error_code	mh_filesystem_check_directory(void *context,
	const char *path, struct mh_error *error_out);

#endif
