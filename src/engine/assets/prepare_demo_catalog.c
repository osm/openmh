#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_catalog.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_menus.h"
#include "engine/assets/prepare_demo_car.h"
#include "engine/assets/prepare_demo_catalog.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

enum {
	DEMO_CARS = 3,
	DEMO_TRACKS = 2,
	CATALOG_FILES = DEMO_CARS + DEMO_TRACKS + 1,
	RECORD_LIMIT = 0x20f0 + 4,
	PATH_SUFFIX_BYTES = 64
};

static void
output_name(char *suffix, unsigned int index, unsigned int tracks)
{
	if (index < tracks)
		sprintf(suffix, "/" MH_ASSET_TRACK_CONFIG_FORMAT, index + 1);
	else if (index < tracks + DEMO_CARS)
		sprintf(suffix, "/" MH_ASSET_CAR_CONFIG_FORMAT,
			index - tracks + 1);
	else
		strcpy(suffix, "/" MH_ASSET_KEYBOARD_CONTROLS);
}

static enum mh_error_code
check_destinations(const struct mh_resource_source *source, char *path,
	size_t prefix, unsigned int tracks, unsigned int count,
	struct mh_error *error_out)
{
	unsigned long size;
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < count; i++) {
		output_name(path + prefix, i, tracks);
		status =
			mh_resource_source_size(source, path, &size, error_out);

		if (status == MH_ERROR_NONE)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"demo catalog destination already exists");

		if (status != MH_ERROR_NOT_FOUND)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * All six early demo readmes specify arrows, Space and F1/F2/F3. The other
 * actions use native keyboard defaults. This is a generated native profile, not
 * a CLO file; the MPlayer demo keeps its shipped profiles.
 */
static enum mh_error_code
prepare_keyboard(struct mh_file *output, struct mh_error *error_out)
{
	static const char text[] =
		"// Native keyboard profile for the early playable demos.\n"
		"LayoutName Keyboard\n"
		"MouseSpeedX 1\nMouseSpeedY 1\nMouseSpeedZ 1\n"
		"ForceFeedback 0\n"
		"TurnLeft LeftArrow\nTurnRight RightArrow\n"
		"Accelerate UpArrow\nBrake DownArrow\n"
		"ShiftGearUp A\nShiftGearDown S\nHorn Return\n"
		"HandBrake Shift\nRearView Space\nGameMenu Esc\n"
		"Info Tab\nChatMode T\nInCarView F1\nOutCarView F2\n"
		"CameraView F3\nRearMirror F5\nCyclePlayers F4\n"
		"ScreenShot F12\n";

	output->data = malloc(sizeof(text) - 1);

	if (output->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing demo keyboard profile");

	memcpy(output->data, text, sizeof(text) - 1);
	output->size = sizeof(text) - 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
convert_records(const struct mh_resource_source *source,
	enum mh_demo_track_layout layout, char *path, size_t prefix,
	unsigned int tracks, struct mh_file files[CATALOG_FILES],
	struct mh_error *error_out)
{
	struct mh_file record;
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < tracks + DEMO_CARS; i++) {
		if (layout == MH_DEMO_TRACK_MPLAYER)
			sprintf(path + prefix,
				"/" MH_ASSET_MPLAYER_SCREEN_FORMAT, i + 1);
		else
			sprintf(path + prefix, "/" MH_ASSET_DEMO_SCREEN_FORMAT,
				i + 1);

		status = mh_resource_source_read_bounded(
			source, path, RECORD_LIMIT, &record, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (i < tracks)
			status = mh_demo_track_prepare(
				&record, layout, &files[i], error_out);
		else
			status = mh_demo_car_prepare(
				&record, &files[i], error_out);

		free(record.data);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (layout != MH_DEMO_TRACK_MPLAYER)
		return prepare_keyboard(&files[tracks + DEMO_CARS], error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
write_catalog(const struct mh_user_data *data, char *path, size_t prefix,
	unsigned int tracks, unsigned int count,
	const struct mh_file files[CATALOG_FILES], struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	strcpy(path + prefix, "/" MH_ASSET_GAME_DIR);
	status = mh_user_data_create_directory(data, path, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < count; i++) {
		output_name(path + prefix, i, tracks);
		status = mh_user_data_replace(
			data, path, files[i].data, files[i].size, error_out);
	}

	return status;
}

enum mh_error_code
mh_demo_catalog_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	struct mh_file files[CATALOG_FILES];
	char *path;
	size_t prefix;
	unsigned int tracks, count, i;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == '\0' ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo catalog preparation request");

	prefix = strlen(root);

	if (prefix > (size_t)-1 - PATH_SUFFIX_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo catalog path is too long");

	path = malloc(prefix + PATH_SUFFIX_BYTES);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing demo catalog paths");

	memcpy(path, root, prefix);
	memset(files, 0, sizeof(files));
	tracks =
		recipe->track_layout == MH_DEMO_TRACK_MPLAYER ? 1 : DEMO_TRACKS;
	count = tracks + DEMO_CARS +
		(recipe->track_layout != MH_DEMO_TRACK_MPLAYER);
	status = check_destinations(
		source, path, prefix, tracks, count, error_out);

	if (status == MH_ERROR_NONE)
		status = convert_records(source, recipe->track_layout, path,
			prefix, tracks, files, error_out);

	if (status == MH_ERROR_NONE)
		status = write_catalog(
			data, path, prefix, tracks, count, files, error_out);

	for (i = 0; i < CATALOG_FILES; i++)
		free(files[i].data);

	free(path);

	return status;
}
