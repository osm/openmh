#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_presentation.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_menus.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/assets/prepare_demo_results.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

enum {
	PATH_BYTES = 1024,
	BOARD_COUNT = 2
};

static const char *board_names[BOARD_COUNT] = {
	MH_ASSET_RESULTS_BACKDROP_NAME, MH_ASSET_RESULTS_TWO_BACKDROP_NAME};

static enum mh_error_code
board_paths(const struct mh_resource_source *source, const char *directory,
	char paths[BOARD_COUNT][PATH_BYTES], struct mh_error *error_out)
{
	size_t length;
	unsigned int i;
	int found;
	enum mh_error_code status;

	length = strlen(directory);

	for (i = 0; i < BOARD_COUNT; i++) {
		status = mh_asset_path_find_from(source, directory,
			board_names[i], paths[i], PATH_BYTES, &found,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (found)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"demo results destination already exists");

		if (length + 1 + strlen(board_names[i]) >= PATH_BYTES)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"demo results path is too long");

		memcpy(paths[i], directory, length);
		paths[i][length] = '/';
		strcpy(paths[i] + length + 1, board_names[i]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_results_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	char directory[PATH_BYTES], paths[BOARD_COUNT][PATH_BYTES];
	struct mh_file file;
	unsigned int i;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == '\0' ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo results preparation request");

	if (recipe->track_layout == MH_DEMO_TRACK_MPLAYER)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_asset_path_from(source, root, MH_ASSET_DATA_DIR, directory,
		sizeof(directory), error_out);

	if (status == MH_ERROR_NONE)
		status = board_paths(source, directory, paths, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < BOARD_COUNT && status == MH_ERROR_NONE; i++) {
		status = mh_demo_results_tga(source, root, i, &file, error_out);

		if (status != MH_ERROR_NONE)
			break;

		status = mh_user_data_replace(
			data, paths[i], file.data, file.size, error_out);
		free(file.data);
	}

	return status;
}
