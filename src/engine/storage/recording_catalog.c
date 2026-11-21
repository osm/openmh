#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/path_names.h"
#include "engine/storage/recording_catalog.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define DEMO_EXTENSION_LENGTH (sizeof(MH_RECORDING_EXTENSION) - 1)
#define RECORDING_FILENAME_SIZE (MH_MENU_DEMO_NAME_SIZE + DEMO_EXTENSION_LENGTH)

enum recording_source {
	SOURCE_INSTALLED,
	SOURCE_USER,
	SOURCE_COUNT
};

struct recording_file {
	char			filename[RECORDING_FILENAME_SIZE];
	enum recording_source	source;
	int			removable;
	struct mh_data_file	*identity;
};

struct mh_recording_files {
	struct mh_data_directory	*directories[SOURCE_COUNT];
	unsigned int			count;
	struct recording_file		entries[MH_MENU_DEMO_MAX_FILES];
};

struct recording_scan {
	struct mh_recording_files	*files;
	enum recording_source		source;
	struct mh_error			error;
};

void
mh_recording_catalog_close(struct mh_recording_catalog *catalog)
{
	unsigned int i;

	if (catalog == NULL)
		return;

	if (catalog->files != NULL) {
		for (i = 0; i < catalog->files->count; i++)
			mh_data_file_close(catalog->files->entries[i].identity);

		for (i = 0; i < SOURCE_COUNT; i++)
			mh_data_directory_close(catalog->files->directories[i]);

		free(catalog->files);
	}

	mh_menu_demo_catalog_free(&catalog->menu);
	memset(catalog, 0, sizeof(*catalog));
}

static int
has_demo_extension(const char *filename, size_t length)
{
	if (length < DEMO_EXTENSION_LENGTH)
		return 0;

	return mh_ascii_equal(filename + length - DEMO_EXTENSION_LENGTH,
		MH_RECORDING_EXTENSION);
}

static enum mh_directory_action
scan_entry(void *context, const char *name)
{
	struct recording_scan *scan;
	struct recording_file *entry;
	struct mh_data_file *identity;
	enum mh_error_code status;
	size_t length;
	int removable;

	scan = context;
	length = strlen(name);

	if (!has_demo_extension(name, length))
		return MH_DIRECTORY_CONTINUE;

	if (length >= sizeof(scan->files->entries[0].filename) ||
		scan->files->count == MH_MENU_DEMO_MAX_FILES) {
		mh_error_set(&scan->error, MH_ERROR_RANGE,
			"recording catalog exceeds native bounds");
		return MH_DIRECTORY_STOP;
	}

	status = mh_data_directory_inspect(
		scan->files->directories[scan->source], name, &identity,
		&removable, &scan->error);

	if (status == MH_ERROR_NOT_FOUND) {
		scan->error = mh_success;
		return MH_DIRECTORY_CONTINUE;
	}

	if (status != MH_ERROR_NONE)
		return MH_DIRECTORY_STOP;

	entry = &scan->files->entries[scan->files->count++];
	strcpy(entry->filename, name);
	entry->source = scan->source;
	entry->removable = scan->source == SOURCE_USER && removable;
	entry->identity = identity;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
scan_directory(struct mh_recording_files *files, enum recording_source source,
	struct mh_error *error_out)
{
	struct recording_scan scan;
	enum mh_error_code status;

	if (files->directories[source] == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_data_directory_path(files->directories[source]) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"recording directory locator is unavailable");

	scan.files = files;
	scan.source = source;
	scan.error = mh_success;
	status = mh_data_directory_list(
		files->directories[source], scan_entry, &scan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_error_forward(error_out, &scan.error);
}

enum mh_error_code
mh_recording_catalog_read_in(struct mh_data_directory *installed,
	struct mh_data_directory *user, const char *saved_name,
	struct mh_recording_catalog *output, struct mh_error *error_out)
{
	struct mh_recording_catalog next;
	const char *names[MH_MENU_DEMO_MAX_FILES];
	enum mh_error_code status;
	unsigned int i;
	int same;

	if (saved_name == NULL || output == NULL ||
		(installed != NULL && installed == user))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording catalog directories/output");

	memset(&next, 0, sizeof(next));
	next.files = calloc(1, sizeof(*next.files));

	if (next.files == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory scanning recordings");

	next.files->directories[SOURCE_INSTALLED] = installed;
	next.files->directories[SOURCE_USER] = user;
	same = 0;
	status = MH_ERROR_NONE;

	if (installed != NULL && user != NULL)
		status = mh_data_directory_same(
			installed, user, &same, error_out);

	if (status == MH_ERROR_NONE)
		status =
			scan_directory(next.files, SOURCE_INSTALLED, error_out);

	if (status == MH_ERROR_NONE && !same)
		status = scan_directory(next.files, SOURCE_USER, error_out);

	for (i = 0; i < next.files->count; i++)
		names[i] = next.files->entries[i].filename;

	if (status == MH_ERROR_NONE)
		status = mh_menu_demo_catalog_read(names, next.files->count,
			saved_name, &next.menu, error_out);

	if (status != MH_ERROR_NONE) {
		/* Failure preserves the caller's directories and output. */
		next.files->directories[SOURCE_INSTALLED] = NULL;
		next.files->directories[SOURCE_USER] = NULL;
		mh_recording_catalog_close(&next);
		return status;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_catalog_path(const struct mh_recording_catalog *catalog,
	unsigned int index, char *path, size_t capacity,
	struct mh_error *error_out)
{
	const struct recording_file *entry;
	const char *directory;
	size_t directory_length, filename_length, separator;

	if (catalog == NULL || catalog->files == NULL ||
		index >= catalog->menu.count || path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording selection");

	entry = &catalog->files->entries[index];
	directory = mh_data_directory_path(
		catalog->files->directories[entry->source]);
	directory_length = strlen(directory);
	filename_length = strlen(entry->filename);
	separator =
		directory_length != 0 && directory[directory_length - 1] != '/';

	if (directory_length >= capacity ||
		filename_length >= capacity - directory_length ||
		capacity - directory_length - filename_length <= separator)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recording path output too small");

	memcpy(path, directory, directory_length);

	if (separator)
		path[directory_length++] = '/';

	memcpy(path + directory_length, entry->filename, filename_length + 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_recording_catalog_removable(
	const struct mh_recording_catalog *catalog, unsigned int index)
{
	return catalog != NULL && catalog->files != NULL &&
		index < catalog->menu.count &&
		catalog->files->entries[index].removable;
}

enum mh_error_code
mh_recording_catalog_remove(struct mh_recording_catalog *catalog,
	unsigned int index, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (!mh_recording_catalog_removable(catalog, index))
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"Recording is read-only.");

	status = mh_data_file_remove_entry(
		catalog->files->entries[index].identity, error_out);

	if (status == MH_ERROR_STATE)
		return mh_error_set(error_out, status,
			"Recording changed. Please select it again.");

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "Cannot delete recording.");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_catalog_load(const struct mh_recording_catalog *catalog,
	unsigned int index, struct mh_file *output, struct mh_error *error_out)
{
	const struct recording_file *entry;
	struct mh_data_file *identity;
	enum mh_error_code status;

	if (catalog == NULL || catalog->files == NULL ||
		index >= catalog->menu.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording selection");

	entry = &catalog->files->entries[index];
	identity = NULL;
	status = mh_data_directory_read(
		catalog->files->directories[entry->source], entry->filename,
		MH_RESOURCE_READ_LIMIT, output, &identity, error_out);
	mh_data_file_close(identity);

	return status;
}

enum mh_error_code
mh_recording_catalog_read_from(const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *installed, const char *user, const char *saved,
	struct mh_recording_catalog *output, struct mh_error *error_out)
{
	struct mh_data_directory *installed_directory, *user_directory;
	enum mh_error_code status;

	installed_directory = user_directory = NULL;
	status = MH_ERROR_NONE;

	if (installed != NULL)
		status = mh_resource_source_open_directory(
			resources, installed, &installed_directory, error_out);

	if (status == MH_ERROR_NOT_FOUND)
		status = MH_ERROR_NONE;

	if (status == MH_ERROR_NONE && user != NULL)
		status = mh_user_data_browse_directory(
			data, user, asset_root, &user_directory, error_out);

	if (status == MH_ERROR_NOT_FOUND)
		status = MH_ERROR_NONE;

	if (status == MH_ERROR_NONE)
		status = mh_recording_catalog_read_in(installed_directory,
			user_directory, saved, output, error_out);

	if (status != MH_ERROR_NONE) {
		mh_data_directory_close(installed_directory);
		mh_data_directory_close(user_directory);
		return mh_error_set(
			error_out, status, "cannot load recording catalog");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recording_catalog_same_entry(
	const struct mh_recording_catalog *first_catalog, unsigned int first,
	const struct mh_recording_catalog *second_catalog, unsigned int second,
	int *same, struct mh_error *error_out)
{
	const struct recording_file *first_entry, *second_entry;

	if (first_catalog == NULL || second_catalog == NULL ||
		first_catalog->files == NULL || second_catalog->files == NULL ||
		first >= first_catalog->menu.count ||
		second >= second_catalog->menu.count || same == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording selections for comparison");

	first_entry = &first_catalog->files->entries[first];
	second_entry = &second_catalog->files->entries[second];

	if (first_entry->source != second_entry->source ||
		strcmp(first_entry->filename, second_entry->filename) != 0) {
		*same = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_data_directory_same(
		first_catalog->files->directories[first_entry->source],
		second_catalog->files->directories[second_entry->source], same,
		error_out);
}
