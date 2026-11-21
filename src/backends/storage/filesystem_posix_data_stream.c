#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "backends/storage/filesystem_posix_data_directory_internal.h"

struct native_stream {
	struct native_temporary	temporary;

	/* Owns the open stream until seal closes it; then NULL. */
	FILE			*file;
	size_t			size;
};

static enum mh_error_code
open_stream(void *context, void **output, struct mh_error *error_out)
{
	struct native_stream *stream;
	enum mh_error_code status;
	int fd;

	stream = calloc(1, sizeof(*stream));

	if (stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening private stream");

	status = mh_filesystem_temporary_open(
		context, &stream->temporary, &fd, error_out);

	if (status != MH_ERROR_NONE) {
		free(stream);
		return status;
	}

	stream->file = fdopen(fd, "wb");

	if (stream->file == NULL) {
		close(fd);
		mh_filesystem_temporary_remove(context, &stream->temporary);
		free(stream);
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot open private stream");
	}

	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_stream(void *context, void *token, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct native_stream *stream;

	(void)context;
	stream = token;

	if (size > (size_t)LONG_MAX - stream->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"private stream exceeds native seek range");

	if (fwrite(bytes, 1, size, stream->file) != size)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot append private stream");

	stream->size += size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
write_stream_at(void *context, void *token, size_t offset, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	struct native_stream *stream;

	(void)context;
	stream = token;

	if (fseek(stream->file, (long)offset, SEEK_SET) != 0 ||
		fwrite(bytes, 1, size, stream->file) != size ||
		fseek(stream->file, (long)stream->size, SEEK_SET) != 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot patch private stream");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
seal_stream(void *context, void *token, size_t size, struct mh_error *error_out)
{
	struct native_stream *stream;
	struct stat identity;
	enum mh_error_code status;

	(void)context;
	stream = token;
	status = MH_ERROR_NONE;

	if (fflush(stream->file) != 0 || fsync(fileno(stream->file)) != 0 ||
		fstat(fileno(stream->file), &identity) != 0)
		status = mh_error_set(error_out, MH_ERROR_IO,
			"cannot finalize private stream");
	else if (identity.st_size < 0 ||
		(long double)identity.st_size != (long double)size)
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"private stream length changed");
	else
		stream->temporary.identity = identity;

	if (fclose(stream->file) != 0 && status == MH_ERROR_NONE)
		status = mh_error_set(
			error_out, MH_ERROR_IO, "cannot close private stream");

	stream->file = NULL;

	if (status != MH_ERROR_NONE)
		return status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_stream(void *context, void *token, const char *name,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *directory, void **output,
	struct mh_error *error_out)
{
	struct native_stream *stream;
	struct native_file *file;
	enum mh_error_code status;

	stream = token;
	file = mh_filesystem_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory publishing private stream");

	status = mh_filesystem_temporary_publish(context, &stream->temporary,
		file, 0, check, checker, directory, error_out);

	if (status != MH_ERROR_NONE) {
		mh_filesystem_file_close(NULL, file);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
replace_stream(void *context, void *token, void *file,
	mh_data_publish_check check, void *checker,
	struct mh_data_directory *directory, struct mh_error *error_out)
{
	struct native_stream *stream;

	stream = token;

	return mh_filesystem_temporary_publish(context, &stream->temporary,
		file, 1, check, checker, directory, error_out);
}

static void
close_stream(void *context, void *token)
{
	struct native_stream *stream;

	stream = token;

	if (stream->file != NULL)
		fclose(stream->file);

	mh_filesystem_temporary_remove(context, &stream->temporary);
	free(stream);
}

const struct mh_data_stream_ops mh_filesystem_stream_ops = {open_stream,
	append_stream, write_stream_at, seal_stream, create_stream,
	replace_stream, close_stream};
