#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_font.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/prepare_demo_font.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define FONT_PATH_BYTES 1024
#define FONT_SOURCE_LIMIT (1024UL * 1024)

enum mh_error_code
mh_demo_font_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	struct mh_file original, prepared;
	char directory[FONT_PATH_BYTES], path[FONT_PATH_BYTES];
	size_t length;
	int found;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo font preparation request");

	if (recipe->track_layout == MH_DEMO_TRACK_MPLAYER)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_asset_path_from(source, root, MH_ASSET_DATA_DIR, directory,
		sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_asset_path_find_from(source, directory,
		MH_ASSET_FONT1S_NAME, path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"demo HUD font destination already exists");

	length = strlen(directory);

	if (length > sizeof(path) - sizeof("/" MH_ASSET_FONT1S_NAME))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo HUD font path is too long");

	status = mh_asset_path_from(source, directory, MH_ASSET_FONT2S_NAME,
		path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		source, path, FONT_SOURCE_LIMIT, &original, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_byte_pixels(&original, &prepared, error_out);
	free(original.data);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(path, directory, length);
	memcpy(path + length, "/" MH_ASSET_FONT1S_NAME,
		sizeof("/" MH_ASSET_FONT1S_NAME));
	status = mh_user_data_replace(
		data, path, prepared.data, prepared.size, error_out);
	free(prepared.data);

	return status;
}
