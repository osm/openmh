#include <stdlib.h>

#include "engine/storage/settings_file.h"
#include "interfaces/storage/user_data.h"

#define SETTINGS_FILE_LIMIT 1048576UL

enum mh_error_code
mh_settings_file_load_from(const struct mh_user_data *data, const char *path,
	const struct mh_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, int *present, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	if (data == NULL || path == NULL || path[0] == 0 || catalog == NULL ||
		random == NULL || settings == NULL || present == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings file arguments");

	status = mh_user_data_read(
		data, path, SETTINGS_FILE_LIMIT, &file, error_out);

	if (status == MH_ERROR_NOT_FOUND) {
		*present = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_settings_parse(
		file.data, file.size, catalog, random, settings, error_out);
	free(file.data);

	if (status == MH_ERROR_NONE)
		*present = 1;

	return status;
}

enum mh_error_code
mh_settings_file_save_to(const struct mh_user_data *data, const char *path,
	const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings, struct mh_error *error_out)
{
	char text[MH_SETTINGS_TEXT_CAPACITY];
	size_t size;
	enum mh_error_code status;

	if (data == NULL || path == NULL || path[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings output path");

	status = mh_settings_encode(
		settings, catalog, text, sizeof(text), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_user_data_replace(data, path, text, size, error_out);
}
