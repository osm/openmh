#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_presentation.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_menus.h"
#include "engine/assets/prepare_demo_loading.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define PATH_BYTES 1024

static enum mh_error_code
output_path(const struct mh_resource_source *source, const char *directory,
	const char *name, char path[PATH_BYTES], struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t length;
	int found;

	status = mh_asset_path_find_from(
		source, directory, name, path, PATH_BYTES, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"demo loading destination already exists");

	length = strlen(directory);

	if (length + 1 + strlen(name) >= PATH_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo loading path is too long");

	memcpy(path, directory, length);
	path[length] = '/';
	strcpy(path + length + 1, name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_loading_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	char directory[PATH_BYTES], background[PATH_BYTES], markers[PATH_BYTES];
	struct mh_file file;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == 0 ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo loading preparation request");

	if (recipe->track_layout == MH_DEMO_TRACK_MPLAYER)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_asset_path_from(source, root, MH_ASSET_DATA_DIR, directory,
		sizeof(directory), error_out);

	if (status == MH_ERROR_NONE)
		status = output_path(source, directory,
			MH_ASSET_LOADING_BACKDROP_NAME, background, error_out);

	if (status == MH_ERROR_NONE)
		status = output_path(source, directory,
			MH_ASSET_LOADING_ANIMATION_NAME, markers, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_loading_tga(0, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_replace(
		data, background, file.data, file.size, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_loading_tga(1, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_replace(
		data, markers, file.data, file.size, error_out);
	free(file.data);

	return status;
}
