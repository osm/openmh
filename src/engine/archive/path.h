#ifndef MH_ARCHIVE_PATH_H
#define MH_ARCHIVE_PATH_H

#include "interfaces/archive/archive.h"

/* Join an optional DOS directory and a basename; reject unsafe components. */
enum mh_error_code	mh_archive_path(const char *directory, const char *name,
	char output[MH_ARCHIVE_PATH_SIZE], struct mh_error *error_out);

#endif
