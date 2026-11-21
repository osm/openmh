#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_stream.h"
#include "interfaces/storage/resource_backend.h"
#ifdef MH_FILESYSTEM_PLATFORM
#include "backends/storage/filesystem_platform.h"
#endif
#ifdef MH_FILESYSTEM_DATA_DIRECTORY
#include "interfaces/storage/data_directory_backend.h"
#endif
#include "backends/storage/filesystem_provider.h"
#include "engine/core/error.h"

static enum mh_error_code
open_resource(const char *path, FILE **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_filesystem_open_read(path, output, NULL);

	return mh_error_set(error_out, status,
		status == MH_ERROR_NONE ? NULL : "cannot open resource");
}

static enum mh_error_code
read_at(void *context, const char *path, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	FILE *stream;
	unsigned long length;
	int failed;
	enum mh_error_code status;

	(void)context;

	status = open_resource(path, &stream, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_filesystem_stream_size(stream, &length, error_out);

	if (status != MH_ERROR_NONE) {
		fclose(stream);
		return status;
	}

	if (offset > length || size > length - offset) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource range outside available bytes");
	}

	failed = mh_filesystem_stream_seek(stream, offset, NULL) !=
			MH_ERROR_NONE ||
		fread(output, 1, size, stream) != size;

	if (fclose(stream) != 0)
		failed = 1;

	if (failed)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot read resource range");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_bytes(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file bytes;
	enum mh_error_code status;
	FILE *stream;
	unsigned long length;

	(void)context;
	status = open_resource(path, &stream, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_filesystem_stream_size(stream, &length, error_out);

	if (status != MH_ERROR_NONE) {
		fclose(stream);
		return status;
	}

	if (length > limit) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"resource exceeds read limit or cannot be positioned");
	}

	bytes.size = (size_t)length;
	bytes.data = malloc(bytes.size == 0 ? 1 : bytes.size);

	if (bytes.data == NULL) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading resource");
	}

	status = fread(bytes.data, 1, bytes.size, stream) == bytes.size
		? MH_ERROR_NONE
		: mh_error_set(error_out, MH_ERROR_IO,
			"cannot read complete resource");

	if (fclose(stream) != 0 && status == MH_ERROR_NONE)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "cannot close resource");

	if (status != MH_ERROR_NONE) {
		free(bytes.data);
		return status;
	}

	*output = bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
file_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	FILE *stream;
	unsigned long size;
	enum mh_error_code status;

	(void)context;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing resource size arguments");

	status = open_resource(path, &stream, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_filesystem_stream_size(stream, &size, error_out);

	if (fclose(stream) != 0 && status == MH_ERROR_NONE)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "cannot close resource");

	if (status != MH_ERROR_NONE)
		return status;

	*output = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

#ifdef MH_FILESYSTEM_DATA_DIRECTORY
static enum mh_error_code
open_directory(void *context, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	(void)context;

	path = path[0] == 0 ? "." : path;

	return mh_filesystem_data_directory_browse(
		path, path, 1, output, error_out);
}
#endif

const struct mh_resource_source *
mh_filesystem_resources(void)
{
	static const struct mh_resource_ops ops = {read_bytes, file_size,
#ifdef MH_FILESYSTEM_PLATFORM
		mh_filesystem_list,
#else
		NULL,
#endif
		read_at,
#ifdef MH_FILESYSTEM_DATA_DIRECTORY
		open_directory
#else
		NULL
#endif
	};
	static const struct mh_resource_source source = {&ops, NULL};

	return &source;
}
