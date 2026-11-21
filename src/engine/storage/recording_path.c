#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/path_names.h"
#include "engine/storage/recording_path.h"
#include "interfaces/storage/user_data.h"

enum {
	RECORDING_DIRECTORY_LIMIT = 960,
	RECORDING_NAME_SIZE = 32,
	RECORDING_LAST_NUMBER = 9999,
	NETWORK_NAME_LIMIT = 64,
	NETWORK_PREFIX_LENGTH = sizeof(MH_RECORDING_NETWORK_PREFIX) - 1,
	NETWORK_EXTENSION_LENGTH = sizeof(MH_RECORDING_EXTENSION) - 1
};

struct recording_name {
	const char	*name;
	int		found;
};

enum mh_error_code
mh_recording_directory_from(const struct mh_user_data *data,
	const char *directory, char path[MH_RECORDING_PATH_SIZE],
	struct mh_error *error_out)
{
	char *preferred;
	size_t length;
	enum mh_error_code status;

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording path output");

	preferred = NULL;

	if (directory == NULL) {
		status = mh_user_data_directory(data, &preferred, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		directory = preferred;
	}

	if (strlen(directory) > RECORDING_DIRECTORY_LIMIT) {
		free(preferred);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recording directory too long");
	}

	strcpy(path, directory);
	length = strlen(path);

	while (length > 1 && path[length - 1] == '/')
		path[--length] = 0;

	if (length != 0 && path[length - 1] != '/')
		strcat(path, "/");

	free(preferred);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_directory_action
match_recording(void *context, const char *name)
{
	struct recording_name *match;

	match = context;

	if (!mh_ascii_equal(match->name, name))
		return MH_DIRECTORY_CONTINUE;

	if (match->found)
		return MH_DIRECTORY_FAIL;

	match->found = 1;

	return MH_DIRECTORY_CONTINUE;
}

enum mh_error_code
mh_recording_path_from(const struct mh_user_data *data, const char *directory,
	char path[MH_RECORDING_PATH_SIZE], struct mh_error *error_out)
{
	char base[MH_RECORDING_PATH_SIZE], name[RECORDING_NAME_SIZE];
	struct recording_name match;
	enum mh_error_code status;
	unsigned int number;

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording path output");

	status = mh_recording_directory_from(data, directory, base, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_user_data_create_directory(data, base, NULL) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot create recording directory");

	for (number = 1; number <= RECORDING_LAST_NUMBER; number++) {
		sprintf(name, "race%04u" MH_RECORDING_EXTENSION, number);
		match.name = name;
		match.found = 0;
		status = mh_user_data_list(
			data, base, match_recording, &match, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!match.found) {
			strcpy(path, base);
			strcat(path, name);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"recording directory has no free Race0001..Race9999 name");
}

static enum mh_error_code
validate_network_name(
	const char *command, size_t *name_length, struct mh_error *error_out)
{
	size_t length, stem, i;
	const char *name;

	length = strlen(command);

	if (length < NETWORK_PREFIX_LENGTH + NETWORK_EXTENSION_LENGTH + 1 ||
		length > NETWORK_PREFIX_LENGTH + NETWORK_EXTENSION_LENGTH +
				NETWORK_NAME_LIMIT ||
		memcmp(command, MH_RECORDING_NETWORK_PREFIX,
			NETWORK_PREFIX_LENGTH) != 0 ||
		strcmp(command + length - NETWORK_EXTENSION_LENGTH,
			MH_RECORDING_EXTENSION) != 0)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network recording name must contain 1 to 64 characters");

	stem = length - NETWORK_PREFIX_LENGTH - NETWORK_EXTENSION_LENGTH;
	name = command + NETWORK_PREFIX_LENGTH;

	for (i = 0; i < stem; i++)
		if ((unsigned char)name[i] < 32 ||
			strchr("/\\:\"<>|?*", name[i]) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"network recording name contains an invalid filename character");

	if (name[stem - 1] == '.' || name[stem - 1] == ' ')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"network recording name cannot end with a dot or space");

	*name_length = stem;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_network_path_from(const struct mh_user_data *data,
	const char *directory, const char *command,
	char path[MH_RECORDING_PATH_SIZE], struct mh_error *error_out)
{
	char base[MH_RECORDING_PATH_SIZE];
	size_t stem, prefix;
	enum mh_error_code status;

	if (command == NULL || path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network recording name");

	stem = 0;
	status = validate_network_name(command, &stem, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_recording_directory_from(data, directory, base, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	prefix = strlen(base);

	if (prefix + stem + sizeof(MH_RECORDING_EXTENSION) >
		MH_RECORDING_PATH_SIZE)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network recording path is too long");

	if (mh_user_data_create_directory(data, base, NULL) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot create recording directory");

	memcpy(path, base, prefix);
	memcpy(path + prefix, command + NETWORK_PREFIX_LENGTH, stem);
	strcpy(path + prefix + stem, MH_RECORDING_EXTENSION);
	mh_ascii_lower(path + prefix);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_path_split(const char *path, char **parent, const char **filename,
	struct mh_error *error_out)
{
	const char *slash, *name;
	char *directory;
	size_t length;

	if (path == NULL || path[0] == 0 || parent == NULL || filename == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording output path");

	slash = strrchr(path, '/');
	name = slash == NULL ? path : slash + 1;

	if (name[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording filename");

	length = slash == NULL ? 0
			: (slash == path ? 1 : (size_t)(slash - path));
	directory = malloc(length + 1);

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening recording");

	memcpy(directory, path, length);
	directory[length] = 0;
	*parent = directory;
	*filename = name;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_destination_open_from(const struct mh_user_data *data,
	const char *asset_root, const char *path,
	struct mh_data_directory **output, const char **filename,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *name;
	char *parent;
	enum mh_error_code status;

	if (output == NULL || filename == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording destination output");

	parent = NULL;
	name = NULL;
	directory = NULL;
	status = mh_recording_path_split(path, &parent, &name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_open_directory(
			data, parent, asset_root, &directory, error_out);

	free(parent);

	if (status != MH_ERROR_NONE)
		return status;

	*output = directory;
	*filename = name;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
