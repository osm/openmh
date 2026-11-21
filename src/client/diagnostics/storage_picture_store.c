#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_picture_store.h"
#include "engine/storage/picture_store.h"
#include "interfaces/storage/data_directory_backend.h"

enum mh_error_code
mh_picture_store_open(const char *path, const char *asset_root,
	struct mh_picture_store **output, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (path == NULL || asset_root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot directory");

	status = mh_filesystem_data_directory_open(
		path, asset_root, &directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_picture_store_open_in(directory, output, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(directory);

	return status;
}
