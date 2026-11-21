#include <stdlib.h>
#include <string.h>

#include "engine/assets/prepare_unpack.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

static enum mh_error_code
write_member(const struct mh_user_data *data, const char *root,
	const char *relative, const struct mh_file *file,
	struct mh_error *error_out)
{
	char *path, *slash;
	size_t root_length, length;
	enum mh_error_code status;

	root_length = strlen(root);
	length = strlen(relative);

	if (root_length > (size_t)-1 - length - 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"extracted member path is too long");

	path = malloc(root_length + length + 2);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating extracted member path");

	memcpy(path, root, root_length);
	path[root_length] = '/';
	memcpy(path + root_length + 1, relative, length + 1);
	slash = strrchr(path, '/');
	*slash = '\0';
	status = mh_user_data_create_directory(data, path, error_out);
	*slash = '/';

	if (status == MH_ERROR_NONE)
		status = mh_user_data_replace(
			data, path, file->data, file->size, error_out);

	free(path);

	return status;
}

enum mh_error_code
mh_archive_unpack(struct mh_archive *archive, const struct mh_user_data *data,
	const char *root, mh_unpack_progress progress, void *context,
	struct mh_error *error_out)
{
	const struct mh_archive_entry *entry;
	struct mh_file file;
	size_t i, count;
	enum mh_error_code status;

	if (archive == NULL || data == NULL || root == NULL || *root == '\0')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid archive unpack arguments");

	count = mh_archive_count(archive);

	for (i = 0; i < count; i++) {
		entry = mh_archive_entry(archive, i);

		if (progress != NULL) {
			status = progress(
				context, entry->path, i, count, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_archive_read(
			archive, i, MH_RESOURCE_READ_LIMIT, &file, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status =
			write_member(data, root, entry->path, &file, error_out);
		free(file.data);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (progress != NULL)
		return progress(context, NULL, count, count, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
