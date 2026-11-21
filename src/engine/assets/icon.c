#include <stdlib.h>

#include "engine/assets/executable_icon.h"
#include "engine/assets/ico.h"
#include "engine/assets/icon.h"
#include "engine/assets/path_installation.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum {
	ICON_PATH_BYTES = 1024,
	ICON_SOURCE_LIMIT = 4 * 1024 * 1024
};

enum mh_error_code
mh_icon_load_from(const struct mh_resource_source *resources, const char *root,
	struct mh_image *output, struct mh_error *error_out)
{
	static const char *const names[] = {
		MH_ASSET_ICON, MH_ASSET_DISC_ICON, MH_ASSET_EXECUTABLE};

	struct mh_file file;
	char path[ICON_PATH_BYTES];
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		status = mh_asset_path_from(resources, root, names[i], path,
			sizeof(path), error_out);

		if (status != MH_ERROR_NONE)
			continue;

		status = mh_resource_source_read_bounded(
			resources, path, ICON_SOURCE_LIMIT, &file, error_out);

		if (status != MH_ERROR_NONE)
			continue;

		if (i == sizeof(names) / sizeof(names[0]) - 1)
			status = mh_executable_icon_decode(
				file.data, file.size, output, error_out);
		else
			status = mh_image_ico_decode(
				file.data, file.size, output, error_out);

		free(file.data);

		if (status == MH_ERROR_NONE)
			return status;
	}

	return status;
}
