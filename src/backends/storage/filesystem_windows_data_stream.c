#include <stdlib.h>

#include "backends/storage/filesystem_windows_data_directory_internal.h"

struct native_stream {
	struct mh_windows_temporary	temporary;
	size_t				size;
};

static enum mh_error_code
open_stream(void *context, void **output, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_stream *stream;
	enum mh_error_code status;

	directory = context;
	status = mh_windows_data_validate(directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	stream = calloc(1, sizeof(*stream));

	if (stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening private stream");

	status = mh_windows_temporary_open(
		directory->handle, &stream->temporary, error_out);

	if (status != MH_ERROR_NONE) {
		free(stream);
		return status;
	}

	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_stream(void *context, void *token, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct native_stream *stream;
	enum mh_error_code status;

	(void)context;
	stream = token;
	status = mh_windows_write_bytes(
		stream->temporary.handle, bytes, size, error_out);

	if (status == MH_ERROR_NONE)
		stream->size += size;

	return status;
}

static enum mh_error_code
write_stream_at(void *context, void *token, size_t offset, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	struct native_stream *stream;
	enum mh_error_code status;

	(void)context;
	stream = token;
	status = mh_windows_seek_file(
		stream->temporary.handle, offset, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_write_bytes(
			stream->temporary.handle, bytes, size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_seek_file(
			stream->temporary.handle, stream->size, error_out);

	return status;
}

static enum mh_error_code
seal_stream(void *context, void *token, size_t size, struct mh_error *error_out)
{
	struct native_stream *stream;

	(void)context;
	stream = token;

	return mh_windows_temporary_seal(&stream->temporary, size, error_out);
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
	file = mh_windows_data_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory publishing private stream");

	status = mh_windows_data_publish(context, &stream->temporary, file, 0,
		check, checker, directory, error_out);

	if (status != MH_ERROR_NONE) {
		mh_windows_data_file_close(NULL, file);
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

	return mh_windows_data_publish(context, &stream->temporary, file, 1,
		check, checker, directory, error_out);
}

static void
close_stream(void *context, void *token)
{
	struct native_stream *stream;

	(void)context;
	stream = token;
	mh_windows_temporary_close(&stream->temporary);
	free(stream);
}

const struct mh_data_stream_ops mh_windows_data_stream_ops = {open_stream,
	append_stream, write_stream_at, seal_stream, create_stream,
	replace_stream, close_stream};
