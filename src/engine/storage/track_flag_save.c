#include <stdlib.h>
#include <string.h>

#include "engine/assets/track_flag.h"
#include "engine/core/text.h"
#include "engine/storage/track_flag_file.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/user_data.h"

#define MARKER_NAME_SIZE 256
/* Capacity including NUL, leaving four bytes for the .FLG extension. */
#define TRACK_NAME_SIZE 252

struct marker_target {
	const char	*wanted;
	char		name[MARKER_NAME_SIZE];
	int		present, ambiguous;
};

static enum mh_error_code
build_marker_name(const char *track_name, char output[MARKER_NAME_SIZE],
	struct mh_error *error_out)
{
	size_t length;

	for (length = 0; length < TRACK_NAME_SIZE && track_name[length] != 0;
		length++)
		if ((unsigned char)track_name[length] < 32 ||
			strchr("/\\:*?\"<>|", track_name[length]) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid progress track name");

	if (length == 0 || length == TRACK_NAME_SIZE ||
		strcmp(track_name, ".") == 0 || strcmp(track_name, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid progress track name");

	memcpy(output, track_name, length);
	memcpy(output + length, ".flg", 5);
	mh_ascii_lower(output);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_directory_action
inspect_name(void *context, const char *name)
{
	struct marker_target *target;

	target = context;

	if (!mh_ascii_equal(target->wanted, name))
		return MH_DIRECTORY_CONTINUE;

	if (target->present || strlen(name) >= sizeof(target->name)) {
		target->ambiguous = 1;
		return MH_DIRECTORY_STOP;
	}

	strcpy(target->name, name);
	target->present = 1;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
find_target(struct mh_data_directory *directory, const char *wanted,
	struct marker_target *target, struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(target, 0, sizeof(*target));
	target->wanted = wanted;
	strcpy(target->name, wanted);
	status = mh_data_directory_list(directory, inspect_name, target, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot scan progress directory");

	if (target->ambiguous)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ambiguous progress marker name");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_target(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	const struct marker_target *expected;
	struct marker_target current;
	enum mh_error_code status;

	expected = context;
	status = find_target(directory, expected->wanted, &current, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (current.present != expected->present ||
		strcmp(current.name, expected->name) != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"progress marker changed before publication");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_flag_file_save_to(const struct mh_user_data *data,
	const char *asset_root, const char *path, const char *track_name,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	struct mh_data_file *file;
	struct marker_target target;
	unsigned char *bytes;
	char filename[MARKER_NAME_SIZE];
	size_t size;
	enum mh_error_code status;

	if (data == NULL || asset_root == NULL || path == NULL ||
		track_name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid progress marker destination");

	status = build_marker_name(track_name, filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_track_flag_encode(track_name, &bytes, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	directory = NULL;
	file = NULL;
	status = mh_user_data_open_directory(
		data, path, asset_root, &directory, NULL);

	if (status != MH_ERROR_NONE) {
		mh_error_set(
			error_out, status, "cannot open progress directory");
		goto done;
	}

	status = find_target(directory, filename, &target, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	if (target.present) {
		status = mh_data_directory_open_file(
			directory, target.name, &file, NULL);

		if (status != MH_ERROR_NONE) {
			mh_error_set(error_out, status,
				"cannot inspect progress marker");
			goto done;
		}

		status = mh_data_file_replace(
			file, bytes, size, validate_target, &target, NULL);
	} else
		status = mh_data_directory_create(directory, target.name, bytes,
			size, validate_target, &target, &file, NULL);

	/*
	 * Backend diagnostics may belong to the directory being closed below.
	 */
	mh_error_set(error_out, status,
		status == MH_ERROR_NONE ? NULL
					: "cannot publish progress marker");

done:
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	free(bytes);

	return status;
}
