#include <stdlib.h>
#include <string.h>

#include "client/session/progress.h"
#include "engine/assets/path_directories.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/game/league_unlocks.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/track_flag_file.h"
#include "engine/ui/menu_catalog.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define INSTALLED_TRACK_COUNT 8
#define TRACK_DATA_DIRECTORY "/" MH_ASSET_DATA_PREFIX
#define TRACK_FLAG_EXTENSION ".FLG"

struct saved_marker {
	const char	*wanted;
	char		name[512];
	int		found, ambiguous;
};

enum mh_error_code
mh_progress_path_from(const struct mh_user_data *data, const char *directory,
	char output[MH_PROGRESS_PATH_SIZE], struct mh_error *error_out)
{
	char *user_directory;
	char path[MH_PROGRESS_PATH_SIZE];
	size_t length, separator;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing progress directory output");

	if (directory != NULL) {
		length = strlen(directory);

		if (length >= MH_PROGRESS_PATH_SIZE)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid progress directory");

		memcpy(output, directory, length + 1);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_user_data_directory(data, &user_directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(user_directory);
	separator = length != 0 && user_directory[length - 1] != '/';

	if (length > MH_PROGRESS_PATH_SIZE - 1 - separator) {
		free(user_directory);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"progress directory is too long");
	}

	memcpy(path, user_directory, length);
	free(user_directory);

	if (separator)
		path[length++] = '/';

	path[length] = 0;
	status = mh_user_data_create_directory(data, path, error_out);

	if (status == MH_ERROR_NONE)
		strcpy(output, path);

	return status;
}

static enum mh_error_code
read_installed_flag(const struct mh_resource_source *resources,
	const char *search_root, const char *relative_path,
	const char *track_name, int *available, struct mh_error *error_out)
{
	char path[MH_PROGRESS_PATH_SIZE];
	int found;
	enum mh_error_code status;

	status = mh_asset_path_find_from(resources, search_root, relative_path,
		path, sizeof(path), &found, error_out);

	if (status == MH_ERROR_NONE) {
		if (found)
			status = mh_track_flag_resource_load_from(resources,
				path, track_name, available, error_out);
		else
			*available = 0;
	}

	return status;
}

static enum mh_directory_action
find_saved_marker(void *context, const char *name)
{
	struct saved_marker *marker;

	marker = context;

	if (!mh_ascii_equal(marker->wanted, name))
		return MH_DIRECTORY_CONTINUE;

	if (marker->found || strlen(name) >= sizeof(marker->name)) {
		marker->ambiguous = 1;
		return MH_DIRECTORY_STOP;
	}

	strcpy(marker->name, name);
	marker->found = 1;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
read_saved_flag(const struct mh_user_data *data, const char *directory,
	const char *name, const char *track_name, int *available,
	struct mh_error *error_out)
{
	struct saved_marker marker;
	char path[MH_PROGRESS_PATH_SIZE];
	size_t length, separator;
	enum mh_error_code status;

	memset(&marker, 0, sizeof(marker));
	marker.wanted = name;
	status = mh_user_data_list(
		data, directory, find_saved_marker, &marker, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (marker.ambiguous)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ambiguous filename in progress directory");

	if (!marker.found) {
		*available = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	length = strlen(directory);
	separator = length != 0 && directory[length - 1] != '/';

	if (length >= sizeof(path) - strlen(marker.name) - separator)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"progress marker path is too long");

	memcpy(path, directory, length);

	if (separator)
		path[length++] = '/';

	strcpy(path + length, marker.name);

	return mh_track_flag_file_load_from(
		data, path, track_name, available, error_out);
}

enum mh_error_code
mh_progress_available_from(const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_track *track,
	unsigned long *output, struct mh_error *error_out)
{
	char relative_path[512];
	size_t path_size;
	enum mh_error_code status;
	int available;

	if (asset_root == NULL || track == NULL || output == NULL ||
		memchr(track->name, 0, sizeof(track->name)) == NULL ||
		track->name[0] == 0 ||
		memchr(track->base_path, 0, sizeof(track->base_path)) == NULL ||
		strpbrk(track->name, "/\\:*?\"<>|") != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid progress track path");

	if (track->division >= MH_TRACK_CUSTOM_DIVISION) {
		*output = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	path_size = strlen(track->base_path) + strlen(track->name) +
		sizeof(TRACK_DATA_DIRECTORY TRACK_FLAG_EXTENSION);

	if (path_size > sizeof(relative_path))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid progress track path");

	strcpy(relative_path, track->base_path);
	strcat(relative_path, TRACK_DATA_DIRECTORY);
	strcat(relative_path, track->name);
	strcat(relative_path, TRACK_FLAG_EXTENSION);
	status = read_installed_flag(resources, asset_root, relative_path,
		track->name, &available, error_out);

	if (status == MH_ERROR_NONE && !available && directory != NULL) {
		strcpy(relative_path, track->name);
		strcat(relative_path, TRACK_FLAG_EXTENSION);
		status = read_saved_flag(data, directory, relative_path,
			track->name, &available, error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = available != 0;

	return status;
}

static enum mh_error_code
prepare_award(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_league *league,
	struct mh_track tracks[INSTALLED_TRACK_COUNT],
	unsigned int order[INSTALLED_TRACK_COUNT],
	struct mh_league_unlock_plan *plan, struct mh_error *error_out)
{
	unsigned long divisions[INSTALLED_TRACK_COUNT],
		sorted_divisions[INSTALLED_TRACK_COUNT];
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < INSTALLED_TRACK_COUNT; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i + 1, &tracks[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!tracks[i].has_division)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"progress track has no Division");

		divisions[i] = tracks[i].division;
	}

	status = mh_menu_catalog_order(divisions, INSTALLED_TRACK_COUNT, order,
		INSTALLED_TRACK_COUNT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < INSTALLED_TRACK_COUNT; i++)
		sorted_divisions[i] = divisions[order[i]];

	return mh_league_unlock_plan(league, sorted_divisions,
		INSTALLED_TRACK_COUNT, plan, error_out);
}

enum mh_error_code
mh_progress_award_from(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_league *league, int unlock_cheat,
	struct mh_error *error_out)
{
	struct mh_track tracks[INSTALLED_TRACK_COUNT];
	struct mh_league_unlock_plan plan;
	unsigned long available[INSTALLED_TRACK_COUNT];
	unsigned int order[INSTALLED_TRACK_COUNT], i, source_index;
	enum mh_error_code status;
	struct mh_error error, first_error;

	if (asset_root == NULL || league == NULL ||
		(unlock_cheat != 0 && unlock_cheat != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid progress award");

	status = prepare_award(catalog_source, resources, asset_root, league,
		tracks, order, &plan, error_out);

	if (status != MH_ERROR_NONE || plan.count == 0 || unlock_cheat)
		return status;

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"progress save directory is unavailable");

	for (i = 0; i < INSTALLED_TRACK_COUNT; i++) {
		status = mh_progress_available_from(resources, data, asset_root,
			directory, &tracks[i], &available[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	first_error = mh_success;

	for (i = 0; i < plan.count; i++) {
		source_index = order[plan.tracks[i]];

		if (available[source_index] != 0)
			continue;

		status = mh_track_flag_file_save_to(data, asset_root, directory,
			tracks[source_index].name, &error);

		if (status == MH_ERROR_NONE)
			available[source_index] = 1;
		else if (first_error.code == MH_ERROR_NONE)
			first_error = error;
	}

	return mh_error_forward(error_out, &first_error);
}

/* Accept this low-32-bit disc marker size. */
int
mh_progress_special_cars_available(
	const struct mh_resource_source *resources, const char *disc_root)
{
	char path[1024];
	enum mh_error_code status;
	int found;
	unsigned long size;

	status = mh_asset_path_find_from(resources, disc_root, "MENU/MOTOR.BMP",
		path, sizeof(path), &found, NULL);

	if (status != MH_ERROR_NONE || !found)
		return 0;

	status = mh_resource_source_size(resources, path, &size, NULL);

	return status == MH_ERROR_NONE && (size & 0xffffffffUL) == 758696UL;
}
