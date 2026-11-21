#include "backends/navigation/internal.h"
#include "engine/storage/asset_path.h"

#define ASSET_PATH_BYTES 4096

enum mh_error_code
mh_navigation_resource_read(const struct mh_resource_source *resources,
	const char *root, const char *reference, size_t byte_limit,
	struct mh_file *output, struct mh_error *error_out)
{
	char path[ASSET_PATH_BYTES];
	enum mh_error_code status;

	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read_bounded(
		resources, path, byte_limit, output, error_out);
}
