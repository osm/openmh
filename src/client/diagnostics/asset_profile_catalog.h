#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_PROFILE_CATALOG_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_PROFILE_CATALOG_H

#include "engine/catalog/profile_load.h"

/*
 * Native profile loader; selects the filesystem resource source.
 * Catalog publication and RNG order follow the portable _from operation.
 */
enum mh_error_code	mh_profile_catalog_load(const char *root,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile_catalog *output, struct mh_error *error_out);

#endif
