#ifndef MH_ZIP_DEFLATE_H
#define MH_ZIP_DEFLATE_H

#include "interfaces/storage/resource.h"

/*
 * Empty output means storage is cheaper, or the entry exceeds the compression
 * workspace budget. Successful compressed output transfers ownership.
 */
enum mh_error_code	mh_zip_deflate(const struct mh_resource_source *source,
	const char *path, unsigned long size, struct mh_file *output,
	unsigned long *checksum, struct mh_error *error_out);

#endif
