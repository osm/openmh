#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/league_codec.h"
#include "engine/storage/league_store.h"
#include "engine/storage/path_names.h"
#include "interfaces/storage/data_directory.h"

#define LEAGUE_FILENAME_SIZE 128

struct league_file {
	char			filename[LEAGUE_FILENAME_SIZE];

	/* Owned file token, closed before the containing directory. */
	struct mh_data_file	*identity;
};

struct mh_league_store {
	struct mh_data_directory	*directory;
	unsigned int			file_count;
	struct league_file		files[MH_LEAGUE_CATALOG_MAX_ENTRIES];
};

struct league_scan {
	struct mh_league_store	*store;
	struct mh_error		error;
};

struct target_names {
	const char	*filename;
	int		exists, ambiguous;
};

struct league_target {
	struct mh_league_store	*store;
	const char		*filename;
	unsigned int		index;
	int			exists;
};

static int
compare_filenames(const void *left, const void *right)
{
	const struct league_file *first, *second;

	first = (const struct league_file *)left;
	second = (const struct league_file *)right;

	return mh_ascii_compare(first->filename, second->filename);
}

static int
valid_filename(const char *filename)
{
	size_t name_length;

	if (filename == NULL)
		return 0;

	for (name_length = 0; name_length < LEAGUE_FILENAME_SIZE &&
		filename[name_length] != 0;
		name_length++) {
	}

	return name_length >= (sizeof(MH_USER_LEAGUE_EXTENSION) - 1) &&
		name_length < LEAGUE_FILENAME_SIZE &&
		strpbrk(filename, "/\\:") == NULL &&
		mh_ascii_compare(filename + name_length -
				(sizeof(MH_USER_LEAGUE_EXTENSION) - 1),
			MH_USER_LEAGUE_EXTENSION) == 0;
}

static enum mh_error_code
validate_directory(struct mh_league_store *store, struct mh_error *error_out)
{
	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing League save directory");

	return mh_data_directory_validate(store->directory, error_out);
}

void
mh_league_store_close(struct mh_league_store *store)
{
	unsigned int i;

	if (store == NULL)
		return;

	for (i = 0; i < store->file_count; i++)
		mh_data_file_close(store->files[i].identity);

	mh_data_directory_close(store->directory);
	free(store);
}

static enum mh_directory_action
visit_file(void *context, const char *name)
{
	struct league_scan *scan;
	struct mh_league_store *store;
	size_t length;

	scan = context;
	store = scan->store;
	length = strlen(name);

	if (length < (sizeof(MH_USER_LEAGUE_EXTENSION) - 1) ||
		mh_ascii_compare(
			name + length - (sizeof(MH_USER_LEAGUE_EXTENSION) - 1),
			MH_USER_LEAGUE_EXTENSION) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (!valid_filename(name) ||
		store->file_count == MH_LEAGUE_CATALOG_MAX_ENTRIES) {
		mh_error_set(&scan->error, MH_ERROR_RANGE,
			"League save catalog exceeds supported bounds");
		return MH_DIRECTORY_STOP;
	}

	strcpy(store->files[store->file_count++].filename, name);

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
scan_files(struct mh_league_store *store, struct mh_error *error_out)
{
	struct league_scan scan;
	enum mh_error_code status;
	unsigned int i;

	scan.store = store;
	scan.error = mh_success;
	status = mh_data_directory_list(
		store->directory, visit_file, &scan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scan.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &scan.error);

	qsort(store->files, store->file_count, sizeof(store->files[0]),
		compare_filenames);

	for (i = 1; i < store->file_count; i++)
		if (mh_ascii_compare(store->files[i - 1].filename,
			store->files[i].filename) == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous League save filenames");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_document(struct mh_league_store *store, unsigned int index,
	const struct mh_profile *profiles, unsigned int profile_count,
	struct mh_league_document *document, struct mh_error *error_out)
{
	struct league_file *file;
	struct mh_file bytes;
	enum mh_error_code status;

	file = &store->files[index];
	status = mh_data_directory_read(store->directory, file->filename,
		MH_LEAGUE_FILE_LIMIT, &bytes, &file->identity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_bytes_decode(&bytes, file->filename, profiles,
		profile_count, document, error_out);
	free(bytes.data);

	return status;
}

enum mh_error_code
mh_league_store_open_in(struct mh_data_directory *directory,
	const struct mh_profile *profiles, unsigned int profile_count,
	struct mh_league_catalog *catalog, struct mh_league_store **output,
	struct mh_error *error_out)
{
	struct mh_league_store *store;
	struct mh_league_catalog next;
	struct mh_league_document document;
	enum mh_error_code status;
	unsigned int i;

	if (directory == NULL || (profile_count != 0 && profiles == NULL) ||
		catalog == NULL || output == NULL || catalog->entries != NULL ||
		catalog->count != 0 || catalog->selected != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League store input");

	store = calloc(1, sizeof(*store));

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening League saves");

	store->directory = directory;
	memset(&next, 0, sizeof(next));
	status = validate_directory(store, error_out);

	if (status == MH_ERROR_NONE)
		status = scan_files(store, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < store->file_count; i++) {
		status = load_document(store, i, profiles, profile_count,
			&document, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_league_catalog_append(&next, &document,
				store->files[i].filename, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = validate_directory(store, error_out);

	if (status != MH_ERROR_NONE) {
		mh_league_catalog_close(&next);

		/* The caller retains the directory when opening fails. */
		store->directory = NULL;
		mh_league_store_close(store);

		return status;
	}

	*catalog = next;
	*output = store;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_directory_action
inspect_target(void *context, const char *name)
{
	struct target_names *names;

	names = context;

	if (mh_ascii_compare(names->filename, name) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (names->exists || strcmp(names->filename, name) != 0) {
		names->ambiguous = 1;
		return MH_DIRECTORY_STOP;
	}

	names->exists = 1;

	return MH_DIRECTORY_CONTINUE;
}

/* Newly discovered files never authorize replacement of an unread identity. */
static enum mh_error_code
validate_target(struct mh_league_store *store, const char *filename,
	unsigned int *index, int *exists, struct mh_error *error_out)
{
	struct target_names names;
	unsigned int i;
	enum mh_error_code status;

	*index = store->file_count;
	*exists = 0;

	for (i = 0; i < store->file_count; i++)
		if (mh_ascii_compare(store->files[i].filename, filename) == 0) {
			if (strcmp(store->files[i].filename, filename) != 0)
				return mh_error_set(error_out, MH_ERROR_STATE,
					"League filename spelling changed");

			*index = i;
			break;
		}

	memset(&names, 0, sizeof(names));
	names.filename = filename;
	status = mh_data_directory_list(
		store->directory, inspect_target, &names, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (names.ambiguous)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ambiguous League save filename");

	*exists = names.exists;

	if (*index < store->file_count) {
		if (!*exists)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"League save changed; reload it before modifying");

		return mh_data_file_validate(
			store->files[*index].identity, error_out);
	}

	if (*exists)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"League save name is already in use");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_publication(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct league_target *target;
	unsigned int index;
	int exists;
	enum mh_error_code status;

	target = context;
	status = mh_data_directory_validate(directory, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_target(target->store, target->filename,
			&index, &exists, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (index != target->index || exists != target->exists)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"League save target changed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_store_save(struct mh_league_store *store, const char *filename,
	const struct mh_league_document *document, struct mh_error *error_out)
{
	struct league_target target;
	struct mh_file bytes;
	enum mh_error_code status;

	if (!valid_filename(filename) || document == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League save name/document");

	target.store = store;
	target.filename = filename;
	status = validate_directory(store, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_target(store, filename, &target.index,
			&target.exists, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!target.exists &&
		store->file_count == MH_LEAGUE_CATALOG_MAX_ENTRIES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"League save catalog is full");

	status = mh_league_bytes_encode(document, filename, &bytes, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (target.exists)
		status = mh_data_file_replace(
			store->files[target.index].identity, bytes.data,
			bytes.size, check_publication, &target, error_out);
	else
		status = mh_data_directory_create(store->directory, filename,
			bytes.data, bytes.size, check_publication, &target,
			&store->files[target.index].identity, error_out);

	free(bytes.data);

	if (status == MH_ERROR_NONE && !target.exists) {
		strcpy(store->files[target.index].filename, filename);
		store->file_count++;
	}

	return status;
}

enum mh_error_code
mh_league_store_remove(struct mh_league_store *store, const char *filename,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int index;
	int exists;

	if (!valid_filename(filename))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League removal name");

	status = validate_directory(store, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_target(
			store, filename, &index, &exists, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!exists)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"League save is not present");

	status = mh_data_file_remove(store->files[index].identity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_data_file_close(store->files[index].identity);

	if (index + 1 < store->file_count)
		memmove(store->files + index, store->files + index + 1,
			(store->file_count - index - 1) *
				sizeof(store->files[0]));

	store->file_count--;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
