#ifndef MH_ZIP_RESOURCES_H
#define MH_ZIP_RESOURCES_H

#include "interfaces/storage/resource_archive.h"

/*
 * Single-volume stored/deflated ZIP entries. ZIP64/encryption unsupported.
 * Compressed range reads retain one expanded entry, up to the resource limit.
 */
const struct mh_resource_archive_provider	*mh_zip_resource_provider(void);

#endif
