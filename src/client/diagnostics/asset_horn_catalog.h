#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_HORN_CATALOG_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_HORN_CATALOG_H

#include "interfaces/audio/horn_source.h"

/*
 * Native catalog loader; selects the filesystem resource source.
 * Ordering and ownership follow the portable _from operation.
 */
enum mh_error_code	mh_horn_catalog_load(const char *root,
	struct mh_horn_catalog *output, struct mh_error *error_out);

#endif
