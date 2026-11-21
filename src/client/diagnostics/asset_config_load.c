#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_config_load.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_car_config_load(const char *root, unsigned int number, struct mh_car *car,
	struct mh_error *error_out)
{
	return mh_car_config_load_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), root, number, car, error_out);
}

enum mh_error_code
mh_track_config_load(const char *root, unsigned int number,
	struct mh_track *track, struct mh_error *error_out)
{
	return mh_track_config_load_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), root, number, track, error_out);
}
