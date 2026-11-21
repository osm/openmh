#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "engine/catalog/profile_load.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_profile_catalog_load(const char *root, const struct mh_horn_catalog *horns,
	unsigned long *random, struct mh_profile_catalog *output,
	struct mh_error *error_out)
{
	return mh_profile_catalog_load_from(mh_profile_fallback_source(),
		mh_filesystem_resources(), root, horns, random, output,
		error_out);
}
