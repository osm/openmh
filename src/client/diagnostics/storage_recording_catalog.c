#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_recording_catalog.h"
#include "engine/storage/recording_catalog.h"
#include "interfaces/storage/data_directory_backend.h"

static enum mh_error_code
open_optional(const char *path, const char *root, int read_only,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_filesystem_data_directory_browse(
		path, root, read_only, output, error_out);

	return status == MH_ERROR_NOT_FOUND
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: status;
}

enum mh_error_code
mh_recording_catalog_read(const char *asset_root, const char *installed,
	const char *user, const char *saved,
	struct mh_recording_catalog *output, struct mh_error *error_out)
{
	struct mh_data_directory *asset_directory, *installed_directory,
		*user_directory;
	enum mh_error_code status;

	asset_directory = installed_directory = user_directory = NULL;
	status = mh_filesystem_data_directory_browse(
		asset_root, asset_root, 1, &asset_directory, error_out);
	mh_data_directory_close(asset_directory);

	if (status == MH_ERROR_NONE)
		status = open_optional(installed, asset_root, 1,
			&installed_directory, error_out);

	if (status == MH_ERROR_NONE)
		status = open_optional(
			user, asset_root, 0, &user_directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_recording_catalog_read_in(installed_directory,
			user_directory, saved, output, error_out);

	if (status != MH_ERROR_NONE) {
		mh_data_directory_close(installed_directory);
		mh_data_directory_close(user_directory);
		return mh_error_set(
			error_out, status, "cannot load recording catalog");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
