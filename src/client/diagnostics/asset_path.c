#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_path.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_asset_path_find(const char *root, const char *relative, char *resolved,
	size_t capacity, int *found, struct mh_error *error_out)
{
	return mh_asset_path_find_from(mh_filesystem_resources(), root,
		relative, resolved, capacity, found, error_out);
}

enum mh_error_code
mh_asset_path(const char *root, const char *relative, char *resolved,
	size_t capacity, struct mh_error *error_out)
{
	return mh_asset_path_from(mh_filesystem_resources(), root, relative,
		resolved, capacity, error_out);
}
