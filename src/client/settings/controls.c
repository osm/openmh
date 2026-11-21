#include <stdlib.h>
#include <string.h>

#include "client/settings/controls.h"
#include "engine/assets/path_directories.h"
#include "engine/core/text.h"
#include "engine/input/device_choices.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/user_data.h"

#define CONTROL_PATH_SIZE 1024

static enum mh_error_code
control_directory_path(const struct mh_user_data *user_data, char *path,
	size_t capacity, struct mh_error *error_out)
{
	char *user_root;
	size_t length;
	enum mh_error_code status;

	status = mh_user_data_directory(user_data, &user_root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(user_root);

	if (length > capacity - 1) {
		free(user_root);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"control profile path too long");
	}

	memcpy(path, user_root, length);
	free(user_root);

	path[length] = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_profile_directory(const struct mh_user_data *user_data,
	const char *asset_root, const char *directory,
	enum mh_control_storage storage, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	char path[CONTROL_PATH_SIZE];
	int optional;
	enum mh_error_code status;

	if (directory == NULL && storage == MH_CONTROL_SESSION) {
		*output = NULL;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	optional = directory == NULL && storage == MH_CONTROL_EXISTING;

	if (directory == NULL) {
		status = control_directory_path(
			user_data, path, sizeof(path), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (storage == MH_CONTROL_PERSISTENT) {
			status = mh_user_data_create_directory(
				user_data, path, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		directory = path;
	}

	status = mh_user_data_open_directory(
		user_data, directory, asset_root, output, error_out);

	if (optional && status == MH_ERROR_NOT_FOUND) {
		*output = NULL;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return status;
}

enum mh_error_code
mh_control_profiles_restrict(struct mh_control_store *store,
	struct mh_control_catalog *catalog, struct mh_error *error_out)
{
	unsigned char keep[MH_CONTROL_PROFILES];
	unsigned int i, action, excluded;
	int replace_selection;
	enum mh_error_code status;

	if (catalog == NULL || catalog->profiles == NULL ||
		catalog->count == 0 || catalog->count > MH_CONTROL_PROFILES ||
		catalog->selected >= catalog->count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid control catalog");

	excluded = 0;

	for (i = 0; i < catalog->count; i++) {
		keep[i] = 1;

		for (action = 0; action < MH_CONTROL_FILE_BINDINGS; action++)
			if (!mh_input_scan_available(catalog->profiles[i]
					.layout.bindings[action]))
				keep[i] = 0;

		excluded += !keep[i];
	}

	if (!excluded)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	replace_selection = !keep[catalog->selected];
	status = mh_control_store_restrict(store, catalog, keep, error_out);

	if (status == MH_ERROR_NONE && replace_selection)
		for (i = 0; i < catalog->count; i++)
			if (mh_ascii_equal(catalog->profiles[i].layout.name,
				"Keyboard")) {
				catalog->selected = i;
				break;
			}

	return status;
}

enum mh_error_code
mh_control_profiles_open(const struct mh_resource_source *resources,
	const struct mh_user_data *user_data, const char *asset_root,
	const char *directory, enum mh_control_storage storage,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_control_store **output, struct mh_error *error_out)
{
	struct mh_data_directory *writable_directory;
	char installed[CONTROL_PATH_SIZE];
	enum mh_error_code status;

	if (resources == NULL || asset_root == NULL || catalog == NULL ||
		output == NULL || catalog->profiles != NULL ||
		catalog->count != 0 || catalog->selected != 0 ||
		storage < MH_CONTROL_SESSION || storage > MH_CONTROL_PERSISTENT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile arguments");

	status = mh_asset_path_from(resources, asset_root, MH_ASSET_GAME_DIR,
		installed, sizeof(installed), error_out);

	if (status == MH_ERROR_NOT_FOUND) {
		if (strlen(asset_root) >
			sizeof(installed) - sizeof("/" MH_ASSET_GAME_DIR))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"installed controls path is too long");

		strcpy(installed, asset_root);
		strcat(installed, "/" MH_ASSET_GAME_DIR);
		status = MH_ERROR_NONE;
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = open_profile_directory(user_data, asset_root, directory,
		storage, &writable_directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_control_store_open_from(resources, installed,
		writable_directory, selection, catalog, output, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(writable_directory);
	else {
		status = mh_control_profiles_restrict(
			*output, catalog, error_out);

		if (status != MH_ERROR_NONE) {
			mh_control_store_close(*output);
			*output = NULL;
			mh_control_catalog_free(catalog);
		}
	}

	return status;
}

enum mh_error_code
mh_control_profiles_load_from(const struct mh_resource_source *resources,
	const struct mh_user_data *user_data, const char *asset_root,
	const char *directory, struct mh_settings *settings,
	struct mh_control_binding_set *output, struct mh_error *error_out)
{
	struct mh_control_catalog catalog;
	struct mh_control_store *store;
	struct mh_control_binding_set bindings;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing control profile output");

	memset(&catalog, 0, sizeof(catalog));
	store = NULL;
	status = mh_control_profiles_open(resources, user_data, asset_root,
		directory, MH_CONTROL_EXISTING,
		settings == NULL ? "Keyboard" : settings->control_name,
		&catalog, &store, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_control_layout_apply(
			&catalog.profiles[catalog.selected].layout, &bindings,
			error_out);

	if (status == MH_ERROR_NONE) {
		if (settings != NULL)
			strcpy(settings->control_name,
				catalog.profiles[catalog.selected].layout.name);

		*output = bindings;
	}

	mh_control_catalog_free(&catalog);
	mh_control_store_close(store);

	return status;
}
