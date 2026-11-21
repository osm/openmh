#include <stdlib.h>

#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_control_store.h"
#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory_backend.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_control_store_open(const char *asset_root, const char *source_directory,
	const char *user_directory, const char *selection,
	struct mh_control_catalog *catalog, struct mh_control_store **output,
	struct mh_error *error_out)
{
	char *source;
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (catalog == NULL || output == NULL || catalog->profiles != NULL ||
		catalog->count != 0 || catalog->selected != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile store input");

	status = mh_filesystem_resource_directory(
		asset_root, source_directory, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	directory = NULL;

	if (user_directory != NULL)
		status = mh_filesystem_data_directory_open(
			user_directory, asset_root, &directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_control_store_open_from(mh_filesystem_resources(),
			source, directory, selection, catalog, output,
			error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(directory);

	free(source);

	return status;
}
