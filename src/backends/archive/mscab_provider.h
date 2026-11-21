#ifndef MH_MSCAB_PROVIDER_H
#define MH_MSCAB_PROVIDER_H

#include "interfaces/archive/archive.h"

/*
 * Single-folder, single-volume MSZIP cabinets. The archive owns decoded bytes.
 */
enum mh_error_code	mh_mscab_open(const void *bytes, size_t size,
	struct mh_archive **output, struct mh_error *error_out);

#endif
