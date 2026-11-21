#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/prepare_demo_horn.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

enum {
	HORN_PATH_BYTES = 1024
};

static const char no_horn[] =
	"// Native choice: this demo supplies no horn samples.\n"
	"Name None\nSampleName <None>\n";

static enum mh_error_code
prepare_path(const struct mh_resource_source *source, const char *root,
	char path[HORN_PATH_BYTES], struct mh_error *error_out)
{
	char directory[HORN_PATH_BYTES];
	size_t length;
	int found;
	enum mh_error_code status;

	status = mh_asset_path_from(source, root, MH_ASSET_SOUNDS_DIR,
		directory, sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_asset_path_find_from(source, directory,
		MH_ASSET_HORN_SUBDIR, path, HORN_PATH_BYTES, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"demo horn directory already exists");

	length = strlen(directory);

	if (length > HORN_PATH_BYTES -
			sizeof("/" MH_ASSET_HORN_SUBDIR
			"/" MH_ASSET_SILENT_HORN_NAME))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo horn path is too long");

	memcpy(path, directory, length);
	memcpy(path + length, "/" MH_ASSET_HORN_SUBDIR,
		sizeof("/" MH_ASSET_HORN_SUBDIR));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_horn_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	char path[HORN_PATH_BYTES];
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == '\0' ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo horn preparation request");

	if (recipe->track_layout == MH_DEMO_TRACK_MPLAYER)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = prepare_path(source, root, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_create_directory(data, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcat(path, "/" MH_ASSET_SILENT_HORN_NAME);

	return mh_user_data_replace(
		data, path, no_horn, sizeof(no_horn) - 1, error_out);
}
