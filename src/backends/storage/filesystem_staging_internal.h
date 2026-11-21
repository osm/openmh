#ifndef MH_FILESYSTEM_STAGING_INTERNAL_H
#define MH_FILESYSTEM_STAGING_INTERNAL_H

#include "backends/storage/filesystem_staging.h"

#define MH_STAGING_PREFIX "openmotorex_tmp_"

/* Platform implementation; the shared entry point validates the basename. */
enum mh_error_code	mh_filesystem_staging_remove_entry(
	struct mh_filesystem_staging *staging, const char *name,
	struct mh_error *error_out);

#endif
