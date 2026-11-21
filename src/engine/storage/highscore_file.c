#include <stdlib.h>
#include <string.h>

#include "engine/assets/tbl.h"
#include "engine/core/text.h"
#include "engine/game/highscore_store.h"
#include "engine/storage/highscore_file.h"
#include "interfaces/storage/user_data.h"

#define HIGHSCORE_FILE_LIMIT 1048576UL

struct award_storage {
	const struct mh_user_data	*data;
	const char			*directory;
};

static enum mh_error_code
build_path(const struct mh_user_data *data, const char *directory,
	const struct mh_highscore_query *query,
	char basename[MH_HIGHSCORE_FILENAME_SIZE], char **output,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t length, basename_length;
	char *path;

	status = mh_highscore_filename(
		query, basename, MH_HIGHSCORE_FILENAME_SIZE, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score directory");

	mh_ascii_lower(basename);
	length = strlen(directory);
	basename_length = strlen(basename);

	if (length > (size_t)-1 - basename_length - 2)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "high-score path too long");

	status = mh_user_data_check_directory(data, directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path = (char *)malloc(length + basename_length + 2);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory building high-score path");

	memcpy(path, directory, length);

	if (length != 0 && directory[length - 1] != '/')
		path[length++] = '/';

	memcpy(path + length, basename, basename_length + 1);
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_table(const struct mh_file *bytes, const char *basename,
	const struct mh_highscore_query *query,
	struct mh_highscore_table *output, struct mh_error *error_out)
{
	unsigned char *decoded;
	size_t size;
	enum mh_error_code status;

	if (bytes->size < 4 || memcmp(bytes->data, "TBL", 3) != 0)
		return mh_highscore_overlay(query->kind, query->laps,
			bytes->data, bytes->size, output, error_out);

	status = mh_tbl_resource_decode(
		bytes->data, bytes->size, basename, &decoded, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_highscore_overlay(
		query->kind, query->laps, decoded, size, output, error_out);
	free(decoded);

	return status;
}

enum mh_error_code
mh_highscore_file_load_from(const struct mh_user_data *data,
	const char *directory, const struct mh_highscore_query *query,
	struct mh_highscore_table *output, int *present,
	struct mh_error *error_out)
{
	struct mh_highscore_table next;
	struct mh_file bytes;
	char basename[MH_HIGHSCORE_FILENAME_SIZE], *path;
	enum mh_error_code status;
	int found;

	if (output == NULL || present == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing high-score load output");

	path = NULL;
	status = build_path(data, directory, query, basename, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_read(
		data, path, HIGHSCORE_FILE_LIMIT, &bytes, error_out);
	free(path);
	found = status == MH_ERROR_NONE;

	if (status == MH_ERROR_NOT_FOUND)
		status = mh_highscore_defaults(
			query->kind, query->laps, &next, error_out);
	else if (status == MH_ERROR_NONE) {
		status =
			decode_table(&bytes, basename, query, &next, error_out);
		free(bytes.data);
	}

	if (status == MH_ERROR_NONE) {
		*output = next;
		*present = found;
	}

	return status;
}

enum mh_error_code
mh_highscore_file_save_to(const struct mh_user_data *data,
	const char *directory, const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out)
{
	unsigned char bytes[MH_HIGHSCORE_BYTES], *encoded;
	char basename[MH_HIGHSCORE_FILENAME_SIZE], *path;
	size_t size;
	enum mh_error_code status;

	if (table == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing high-score save table");

	if (table->dirty == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_highscore_write(table, bytes, sizeof(bytes), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	path = NULL;
	status = build_path(data, directory, query, basename, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_tbl_encode_named(
		bytes, sizeof(bytes), basename, &encoded, &size, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_user_data_replace(
			data, path, encoded, size, error_out);
		free(encoded);
	}

	free(path);

	return status;
}

static enum mh_error_code
award_load(void *context, const struct mh_highscore_query *query,
	struct mh_highscore_table *table, struct mh_error *error_out)
{
	const struct award_storage *storage;
	int present;

	storage = context;

	return mh_highscore_file_load_from(storage->data, storage->directory,
		query, table, &present, error_out);
}

static enum mh_error_code
award_save(void *context, const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out)
{
	const struct award_storage *storage;

	storage = context;

	return mh_highscore_file_save_to(
		storage->data, storage->directory, query, table, error_out);
}

enum mh_error_code
mh_highscore_file_awards_to(const struct mh_user_data *data,
	const char *directory, struct mh_highscore_awards *awards,
	struct mh_error *error_out)
{
	struct award_storage storage;
	struct mh_highscore_store store;

	storage.data = data;
	storage.directory = directory;
	store.context = &storage;
	store.load = award_load;
	store.save = award_save;

	return mh_highscore_awards_save(awards, &store, error_out);
}
