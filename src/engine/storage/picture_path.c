#include <stdlib.h>
#include <string.h>

#include "engine/storage/picture_path.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/user_data.h"

enum mh_error_code
mh_picture_directory_open_from(const struct mh_user_data *data,
	const char *directory, const char *asset_root,
	char path[MH_PICTURE_DIRECTORY_SIZE], struct mh_data_directory **output,
	struct mh_error *error_out)
{
	struct mh_data_directory *opened;
	char next[MH_PICTURE_DIRECTORY_SIZE], *preferred;
	enum mh_error_code status;
	size_t length;
	int default_path;

	if (path == NULL || output == NULL || asset_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot directory");

	if (data == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"screenshot storage is unavailable");

	default_path = directory == NULL;
	preferred = NULL;

	if (default_path) {
		status = mh_user_data_directory(data, &preferred, NULL);

		if (status != MH_ERROR_NONE)
			return mh_error_set(error_out, status,
				"cannot locate screenshot directory");

		directory = preferred;
	}

	length = strlen(directory);

	if (length > sizeof(next) - 1) {
		free(preferred);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"screenshot directory is too long");
	}

	strcpy(next, directory);
	free(preferred);

	while (length > 1 && next[length - 1] == '/')
		next[--length] = 0;

	status = mh_user_data_open_directory(
		data, next, asset_root, &opened, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot open screenshot directory");

	strcpy(path, next);
	*output = opened;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
