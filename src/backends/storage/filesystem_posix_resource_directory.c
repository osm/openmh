#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_filesystem_resource_directory(const char *asset_root, const char *directory,
	char **output, struct mh_error *error_out)
{
	char *root, *path;
	size_t length;
	enum mh_error_code status;

	if (asset_root == NULL || directory == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid resource directory paths");

	root = realpath(asset_root, NULL);

	if (root == NULL)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot resolve resource root");

	path = realpath(directory, NULL);
	length = strlen(root);

	if (path == NULL)
		status = mh_error_set(error_out, MH_ERROR_IO,
			"cannot resolve resource directory");
	else
		status = MH_ERROR_NONE;

	if (status == MH_ERROR_NONE &&
		(strncmp(path, root, length) != 0 ||
			(length != 1 && path[length] != 0 &&
				path[length] != '/')))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"resource directory must be inside the asset tree");

	free(root);

	if (status != MH_ERROR_NONE) {
		free(path);
		return status;
	}

	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
