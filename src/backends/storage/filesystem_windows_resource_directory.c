#include <stdlib.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/filesystem_windows_handle.h"

enum mh_error_code
mh_filesystem_resource_directory(const char *asset_root, const char *directory,
	char **output, struct mh_error *error_out)
{
	WCHAR *root, *path;
	enum mh_error_code status;

	if (asset_root == NULL || directory == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid resource directory paths");

	status = mh_windows_resolve_directory(asset_root, &root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path = NULL;
	status = mh_windows_resolve_directory(directory, &path, error_out);

	if (status == MH_ERROR_NONE &&
		mh_windows_relative_path(path, root) == NULL)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"resource directory must be inside the asset tree");

	if (status == MH_ERROR_NONE)
		status = mh_windows_path_to_utf8(path, output, error_out);

	free(path);
	free(root);

	return status;
}
