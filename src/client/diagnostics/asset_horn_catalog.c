#include "backends/audio/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_horn_catalog_load(const char *root, struct mh_horn_catalog *output,
	struct mh_error *error_out)
{
	return mh_horn_catalog_load_from(mh_horn_fallback_source(),
		mh_filesystem_resources(), root, output, error_out);
}
