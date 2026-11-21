#ifndef MH_RESOURCE_DIRECTORY_H
#define MH_RESOURCE_DIRECTORY_H

#include "interfaces/storage/resource.h"

/* Read-only catalog adapter. The immutable source must outlive all handles. */
enum mh_error_code	mh_resource_directory_open(
	const struct mh_resource_source *source, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out);

#endif
