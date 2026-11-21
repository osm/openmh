#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "engine/assets/content_probe.h"
#include "engine/assets/packages.h"
#include "engine/storage/path_names.h"
#include "engine/storage/user_root.h"
#include "interfaces/storage/user_data.h"

static enum mh_error_code
default_save_directory(const struct mh_application *app, char **output,
	struct mh_error *error_out)
{
	const char *installation;
	char *path;
	size_t length;

	installation = app->installation_root;

	if (installation == NULL)
		installation = app->asset_root;

	if (installation == NULL || installation[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing installation directory for user data");

	length = strlen(installation);

	if (length > (size_t)-1 - sizeof("/" MH_USER_DATA_DIR))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"installation save directory is too long");

	path = malloc(length + sizeof("/" MH_USER_DATA_DIR));

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory selecting installation saves");

	memcpy(path, installation, length);

	if (path[length - 1] != '/')
		path[length++] = '/';

	strcpy(path + length, MH_USER_DATA_DIR);
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_content_saves(
	struct mh_application *app, struct mh_error *error_out)
{
	const char *directory;
	char *default_directory;
	enum mh_error_code status;

	if (app->content_edition == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"content must be selected before opening saves");

	if (app->content_user_data != NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	directory = app->user_data_directory;
	default_directory = NULL;

	if (directory == NULL) {
		status = default_save_directory(
			app, &default_directory, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		directory = default_directory;
	}

	status = mh_user_root_open(
		app->user_data, directory, &app->content_user_data, error_out);
	free(default_directory);

	if (status != MH_ERROR_NONE)
		return status;

	if (app->session_options.screenshot_data == app->user_data)
		app->session_options.screenshot_data = app->content_user_data;

	app->user_data = app->content_user_data;
	app->session_options.user_data = app->content_user_data;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_load_content(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_content_info info;
	struct mh_vfs *mount;
	enum mh_error_code status;

	mount = app->asset_mount;

	if (mount == NULL) {
		status =
			mh_content_mount(app->resources, app->resource_archives,
				app->asset_root, &mount, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_content_probe(mh_vfs_resources(mount), mh_vfs_root(mount),
		NULL, &info, error_out);

	if (status != MH_ERROR_NONE) {
		if (app->asset_mount == NULL)
			mh_vfs_close(mount);

		return status;
	}

	app->asset_mount = mount;
	app->resources = mh_vfs_resources(mount);
	app->asset_root = mh_vfs_root(mount);
	app->installation_root = mh_vfs_installation(mount);
	app->session_options.resources = app->resources;
	app->content_edition = info.edition;
	app->content_intro = info.intro;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
