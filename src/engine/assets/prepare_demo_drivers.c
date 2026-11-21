#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_drivers.h"
#include "engine/assets/path_catalog.h"
#include "engine/assets/path_menus.h"
#include "engine/assets/prepare_demo_drivers.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/assets/tbl.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

enum {
	DRIVER_PATH_BYTES = 1024,
	DRIVER_TEXT_BYTES = 1024
};

static enum mh_error_code
write_driver(const struct mh_user_data *data, const char *directory,
	unsigned int index, struct mh_error *error_out)
{
	struct mh_profile driver;
	struct mh_file encoded;
	char text[DRIVER_TEXT_BYTES], path[DRIVER_PATH_BYTES], suffix[32];
	size_t size, length;
	unsigned int i;
	unsigned long color;
	enum mh_error_code status;

	sprintf(suffix, "/" MH_ASSET_CPU_PROFILE_NAME_FORMAT, index + 1);
	length = strlen(directory);

	if (length >= sizeof(path) - strlen(suffix))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo driver path is too long");

	status = mh_profile_demo_default(index, &driver, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * The decoded default supplies bounded car names from its fixed roster.
	 */
	sprintf(text,
		"// Native profile from the demo's fixed car/paint roster.\n"
		"PlayerName Computer Player\nPlayerNick CPU%u\n"
		"PlayerPicture " MH_ASSET_PLAYER_PICTURE
		"\nPlayerColour 0 255 255\n"
		"CarDivision0 %s\nCarDivision1 %s\n"
		"CarDivision2 %s\nCarDivision3 %s\nCarHorn None\n",
		index + 1, driver.cars[0], driver.cars[0], driver.cars[0],
		driver.cars[0]);
	size = strlen(text);

	for (i = 0; i < 3; i++) {
		color = driver.colors[i + 1];
		sprintf(text + size, "CarColour%u %lu %lu %lu\n", i + 1,
			(color >> 16) & 255, (color >> 8) & 255, color & 255);
		size += strlen(text + size);
	}

	status = mh_tbl_encode_named((const unsigned char *)text, size, NULL,
		&encoded.data, &encoded.size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(path, directory, length);
	strcpy(path + length, suffix);
	status = mh_user_data_replace(
		data, path, encoded.data, encoded.size, error_out);
	free(encoded.data);

	return status;
}

enum mh_error_code
mh_demo_drivers_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	char path[DRIVER_PATH_BYTES];
	size_t length;
	unsigned int i;
	int found;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == '\0' ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo driver preparation request");

	if (recipe->track_layout == MH_DEMO_TRACK_MPLAYER)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	length = strlen(root);

	if (length >
		sizeof(path) - sizeof("/" MH_ASSET_PROFILES_DIR "/CPU8.adp"))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo driver path is too long");

	status = mh_asset_path_find_from(source, root, MH_ASSET_LEAGUE_DIR,
		path, sizeof(path), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"demo driver directory already exists");

	sprintf(path, "%s/" MH_ASSET_LEAGUE_DIR, root);
	status = mh_user_data_create_directory(data, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcat(path, "/" MH_ASSET_PROFILES_SUBDIR);
	status = mh_user_data_create_directory(data, path, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < MH_DEMO_DRIVERS; i++)
		status = write_driver(data, path, i, error_out);

	return status;
}
