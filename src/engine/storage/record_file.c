#include <stdlib.h>
#include <string.h>

#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "engine/storage/record_file.h"
#include "interfaces/storage/user_data.h"

#define TRACK_NAME_LIMIT 255
/* Separator, two lap digits, extension and NUL. */
#define RECORD_PATH_EXTRA 7
#define TBL_HEADER_SIZE 4

static enum mh_error_code
build_paths(const struct mh_user_data *data, const char *directory,
	const char *track_name, unsigned int laps, char **lap_path,
	char **race_path, struct mh_error *error_out)
{
	size_t prefix_length, track_length, path_size, i;
	char *paths;
	enum mh_error_code status;

	if (directory == NULL || track_name == NULL || track_name[0] == 0 ||
		laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record path or lap count");

	track_length = strlen(track_name);

	if (track_length > TRACK_NAME_LIMIT || strcmp(track_name, ".") == 0 ||
		strcmp(track_name, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record track name");

	for (i = 0; i < track_length; i++)
		if ((unsigned char)track_name[i] < 32 ||
			(unsigned char)track_name[i] > 126 ||
			strchr("/\\:*?\"<>|", track_name[i]) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid record track name");

	prefix_length = strlen(directory);

	if (prefix_length > (size_t)-1 - track_length - RECORD_PATH_EXTRA ||
		prefix_length + track_length + RECORD_PATH_EXTRA >
			(size_t)-1 / 2)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "record path too long");

	status = mh_user_data_check_directory(data, directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path_size = prefix_length + track_length + RECORD_PATH_EXTRA;
	paths = (char *)malloc(path_size * 2);

	if (paths == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory building record paths");

	memcpy(paths, directory, prefix_length);

	if (prefix_length != 0 && directory[prefix_length - 1] != '/')
		paths[prefix_length++] = '/';

	memcpy(paths + prefix_length, track_name, track_length);
	prefix_length += track_length;
	memcpy(paths + path_size, paths, prefix_length);
	memcpy(paths + prefix_length, ".cl", 4);
	paths[path_size + prefix_length] = (char)('0' + laps / 10);
	paths[path_size + prefix_length + 1] = (char)('0' + laps % 10);
	memcpy(paths + path_size + prefix_length + 2, ".cr", 4);
	mh_ascii_lower(paths + prefix_length - track_length);
	mh_ascii_lower(paths + path_size + prefix_length - track_length);

	/* Both paths share this allocation; only lap_path is freed. */
	*lap_path = paths;
	*race_path = paths + path_size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_record(const struct mh_file *file, size_t expected_size,
	unsigned char *output, struct mh_error *error_out)
{
	unsigned char *decoded;
	size_t decoded_size;
	enum mh_error_code status;

	if (file->size >= TBL_HEADER_SIZE &&
		memcmp(file->data, "TBL", 3) == 0) {
		if (file->size != expected_size + TBL_HEADER_SIZE)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid wrapped record size");

		status = mh_tbl_decode(file->data, file->size, &decoded,
			&decoded_size, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (decoded_size != expected_size) {
			free(decoded);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid decoded record size");
		}

		memcpy(output, decoded, expected_size);
		free(decoded);
	} else {
		if (file->size != expected_size)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid record file size");

		memcpy(output, file->data, expected_size);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_record(const struct mh_user_data *data, const char *path,
	size_t expected_size, unsigned char *output, int *present,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_user_data_read(
		data, path, expected_size + TBL_HEADER_SIZE, &file, error_out);

	if (status == MH_ERROR_NOT_FOUND) {
		*present = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = decode_record(&file, expected_size, output, error_out);
	free(file.data);

	if (status == MH_ERROR_NONE)
		*present = 1;

	return status;
}

enum mh_error_code
mh_record_file_load_from(const struct mh_user_data *data, const char *directory,
	const char *track_name, unsigned int laps, struct mh_records *output,
	struct mh_error *error_out)
{
	struct mh_records next;
	unsigned char bytes[MH_RECORD_RACE_BYTES];
	char *lap_path, *race_path;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing record output");

	status = build_paths(data, directory, track_name, laps, &lap_path,
		&race_path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	status = read_record(data, lap_path, MH_RECORD_LAP_BYTES, bytes,
		&next.has_lap, error_out);

	if (status == MH_ERROR_NONE && next.has_lap)
		status = mh_lap_record_read(
			bytes, MH_RECORD_LAP_BYTES, &next.lap, error_out);

	if (status == MH_ERROR_NONE)
		status = read_record(data, race_path, MH_RECORD_RACE_BYTES,
			bytes, &next.has_race, error_out);

	if (status == MH_ERROR_NONE && next.has_race)
		status = mh_race_record_read(
			bytes, MH_RECORD_RACE_BYTES, &next.race, error_out);

	free(lap_path);

	if (status == MH_ERROR_NONE)
		*output = next;

	return status;
}

static enum mh_error_code
write_record(const struct mh_user_data *data, const char *path,
	const unsigned char *bytes, size_t size, struct mh_error *error_out)
{
	enum mh_error_code status;

	/* A second write must not invalidate the first failure's diagnostic. */
	status = mh_user_data_replace(data, path, bytes, size, NULL);

	return mh_error_set(error_out, status,
		status == MH_ERROR_NONE ? NULL : "cannot replace record file");
}

enum mh_error_code
mh_record_file_save_to(const struct mh_user_data *data, const char *directory,
	const char *track_name, unsigned int laps,
	const struct mh_records *records, struct mh_error *error_out)
{
	unsigned char lap_data[MH_RECORD_LAP_BYTES],
		race_data[MH_RECORD_RACE_BYTES];
	char *lap_path, *race_path;
	enum mh_error_code status, race_status;

	if (records == NULL ||
		(records->has_lap != 0 && records->has_lap != 1) ||
		(records->has_race != 0 && records->has_race != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid record presence");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (records->has_lap)
		status = mh_lap_record_write(
			&records->lap, lap_data, sizeof(lap_data), error_out);

	if (status == MH_ERROR_NONE && records->has_race)
		status = mh_race_record_write(&records->race, race_data,
			sizeof(race_data), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = build_paths(data, directory, track_name, laps, &lap_path,
		&race_path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (records->has_lap)
		status = write_record(
			data, lap_path, lap_data, sizeof(lap_data), error_out);

	if (records->has_race) {
		race_status = write_record(data, race_path, race_data,
			sizeof(race_data),
			status == MH_ERROR_NONE ? error_out : NULL);

		if (status == MH_ERROR_NONE)
			status = race_status;
	}

	free(lap_path);

	return status;
}
