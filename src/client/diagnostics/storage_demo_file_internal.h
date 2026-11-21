#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_FILE_INTERNAL_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_FILE_INTERNAL_H

#include "interfaces/storage/data_directory.h"

/* Owned directory; filename remains borrowed from path. */
enum mh_error_code	mh_diagnostic_demo_directory(const char *path,
	const char *asset_root, struct mh_data_directory **output,
	const char **filename, struct mh_error *error_out);

#endif
