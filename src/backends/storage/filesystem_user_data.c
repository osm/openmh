#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_replace.h"
#include "backends/storage/filesystem_platform.h"
#include "backends/storage/filesystem_provider.h"
#include "engine/storage/path_names.h"
#include "interfaces/storage/data_directory_backend.h"
#include "interfaces/storage/user_data_backend.h"

/* Standalone tools use cwd; the client binds its selected installation root. */
static enum mh_error_code
default_directory(void *context, char **output, struct mh_error *error_out)
{
	char *path;
	enum mh_error_code status;

	(void)context;
	path = malloc(sizeof(MH_USER_DATA_DIR));

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying user-data directory");

	strcpy(path, MH_USER_DATA_DIR);
	status = mh_filesystem_create_directory(NULL, path, error_out);

	if (status == MH_ERROR_NONE)
		*output = path;
	else
		free(path);

	return status;
}

static enum mh_error_code
read_file(void *context, const char *path, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file next;
	FILE *stream;
	enum mh_error_code status;
	long length;
	int failed;

	(void)context;
	status = mh_filesystem_open_read(path, &stream, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = fseek(stream, 0, SEEK_END) == 0 ? ftell(stream) : -1;

	if (length < 0 || (unsigned long)length > limit ||
		fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"user-data file is not regular or exceeds read limit");
	}

	next.data = malloc((size_t)length + 1);

	if (next.data == NULL) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading user data");
	}

	/* Read one extra byte to detect growth since the length query. */
	next.size = fread(next.data, 1, (size_t)length + 1, stream);
	failed = ferror(stream);

	if (fclose(stream) != 0)
		failed = 1;

	if (failed || next.size != (size_t)length) {
		free(next.data);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot read stable user-data file");
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_file(void *context, const char *path, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	static const struct mh_error errors[] = {{MH_ERROR_NONE, NULL},
		{MH_ERROR_RANGE, "user-data path too long"},
		{MH_ERROR_MEMORY, "out of memory building user-data path"},
		{MH_ERROR_IO, "cannot create user-data temporary file"},
		{MH_ERROR_IO, "cannot open user-data temporary stream"},
		{MH_ERROR_IO, "cannot replace user-data file"}};

	enum mh_file_replace_status status;

	(void)context;
	status = mh_file_replace(path, bytes, size);

	return mh_error_forward(error_out, &errors[status]);
}

static enum mh_error_code
open_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	(void)context;

	return mh_filesystem_data_directory_open(
		path, resource_root, output, error_out);
}

static enum mh_error_code
browse_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	(void)context;

	return mh_filesystem_data_directory_browse(
		path, resource_root, 0, output, error_out);
}

const struct mh_user_data *
mh_filesystem_user_data(void)
{
	static const struct mh_user_data_ops ops = {read_file, replace_file,
		default_directory, mh_filesystem_list,
		mh_filesystem_create_directory, open_directory,
		mh_filesystem_check_directory, browse_directory};

	static const struct mh_user_data data = {&ops, NULL};

	return &data;
}
