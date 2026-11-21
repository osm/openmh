#include <stdlib.h>
#include <string.h>

#include "engine/storage/resource_directory.h"
#include "interfaces/storage/data_directory_backend.h"

struct resource_directory {
	const struct mh_resource_source	*source;
	char				*path;
};

static enum mh_directory_action
found_child(void *context, const char *name)
{
	(void)context;
	(void)name;

	return MH_DIRECTORY_STOP;
}

static enum mh_error_code
validate_directory(void *context, struct mh_error *error_out)
{
	/*
	 * The prepared resource table and all its borrowed bytes are immutable.
	 */
	(void)context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
list_directory(void *context, mh_directory_visit visit, void *visitor,
	struct mh_error *error_out)
{
	struct resource_directory *directory;

	directory = context;

	return mh_resource_source_list(
		directory->source, directory->path, visit, visitor, error_out);
}

static enum mh_error_code
entry_path(struct resource_directory *directory, const char *name,
	char **output, struct mh_error *error_out)
{
	char *path;
	size_t directory_length, name_length;

	directory_length = strlen(directory->path);
	name_length = strlen(name);

	if (directory_length > (size_t)-1 - 2 ||
		name_length > (size_t)-1 - directory_length - 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource entry path is too long");

	path = malloc(directory_length + name_length + 2);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening resource entry");

	memcpy(path, directory->path, directory_length);

	if (directory_length != 0)
		path[directory_length++] = '/';

	memcpy(path + directory_length, name, name_length + 1);
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_entry(void *context, const char *name, size_t limit, struct mh_file *bytes,
	void **output, struct mh_error *error_out)
{
	struct resource_directory *directory;
	char *path;
	enum mh_error_code status;

	directory = context;
	path = NULL;
	status = entry_path(directory, name, &path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_bounded(
			directory->source, path, limit, bytes, error_out);

	if (status != MH_ERROR_NONE) {
		free(path);
		return status;
	}

	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_entry(void *context, void *token)
{
	(void)context;
	free(token);
}

static void
close_directory(void *context)
{
	struct resource_directory *directory;

	directory = context;
	free(directory->path);
	free(directory);
}

static enum mh_error_code
validate_entry(void *context, void *token, struct mh_error *error_out)
{
	struct resource_directory *directory;
	unsigned long size;

	directory = context;

	return mh_resource_source_size(
		directory->source, token, &size, error_out);
}

static const char *
directory_path(void *context)
{
	struct resource_directory *directory;

	directory = context;

	return directory->path;
}

static enum mh_error_code
same_directory(
	void *context, void *other, int *same, struct mh_error *error_out)
{
	struct resource_directory *directory, *other_directory;

	directory = context;
	other_directory = other;
	*same = directory->source == other_directory->source &&
		strcmp(directory->path, other_directory->path) == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
inspect_entry(void *context, const char *name, void **output, int *removable,
	struct mh_error *error_out)
{
	struct resource_directory *directory;
	char *path;
	unsigned long size;
	enum mh_error_code status;

	directory = context;
	status = entry_path(directory, name, &path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_size(
		directory->source, path, &size, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND) {
		free(path);
		return status;
	}

	if (status == MH_ERROR_NOT_FOUND) {
		status = mh_resource_source_list(
			directory->source, path, found_child, NULL, error_out);
		free(path);

		if (status != MH_ERROR_NONE)
			return status;

		path = NULL;
	}

	*output = path;
	*removable = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_resource_directory_open(const struct mh_resource_source *source,
	const char *path, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	static const struct mh_data_browse_ops browse = {
		directory_path, same_directory, inspect_entry, NULL};

	static const struct mh_data_directory_ops ops = {validate_directory,
		list_directory, read_entry, NULL, NULL, close_entry,
		close_directory, validate_entry, NULL, NULL, NULL, NULL, NULL,
		&browse};

	struct resource_directory *directory;
	size_t length;
	enum mh_error_code status;

	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid resource directory");

	directory = calloc(1, sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening resource directory");

	directory->source = source;
	length = strlen(path);

	while (length != 0 && path[length - 1] == '/')
		length--;

	directory->path = malloc(length + 1);

	if (directory->path == NULL) {
		free(directory);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying resource directory");
	}

	memcpy(directory->path, path, length);
	directory->path[length] = 0;
	status = list_directory(directory, found_child, NULL, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_bind(
			&ops, directory, output, error_out);

	if (status != MH_ERROR_NONE)
		close_directory(directory);

	return status;
}
