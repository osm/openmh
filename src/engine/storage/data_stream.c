#include <stdlib.h>

#include "engine/storage/data_directory_internal.h"
#include "interfaces/storage/data_stream_backend.h"

enum stream_state {
	STREAM_WRITING,
	/* Failed publication retains the sealed bytes for retry. */
	STREAM_SEALED,
	STREAM_PUBLISHED,
	/* A provider write/seal failure may leave partial state: close only. */
	STREAM_FAILED
};

struct mh_data_stream {
	struct mh_data_directory	*directory;
	void				*token;
	size_t				size;
	enum stream_state		state;
};

enum mh_error_code
mh_data_stream_open(struct mh_data_directory *directory,
	struct mh_data_stream **output, struct mh_error *error_out)
{
	struct mh_data_stream *stream;
	enum mh_error_code status;

	if (directory == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid private stream output");

	if (directory->ops->streams == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"directory has no private stream provider");

	stream = calloc(1, sizeof(*stream));

	if (stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening private stream");

	status = directory->ops->streams->open(
		directory->context, &stream->token, error_out);

	if (status != MH_ERROR_NONE) {
		free(stream);
		return status;
	}

	stream->directory = directory;
	stream->state = STREAM_WRITING;
	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_write(const struct mh_data_stream *stream, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	if (stream == NULL || (bytes == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid private stream write");

	if (stream->state != STREAM_WRITING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stream is not writable");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_stream_append(struct mh_data_stream *stream, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	status = validate_write(stream, bytes, size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > (size_t)-1 - stream->size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"private stream size exceeds addressable range");

	directory = stream->directory;
	status = directory->ops->streams->append(directory->context,
		stream->token, bytes == NULL ? "" : bytes, size, error_out);

	if (status == MH_ERROR_NONE)
		stream->size += size;
	else
		stream->state = STREAM_FAILED;

	return status;
}

enum mh_error_code
mh_data_stream_write_at(struct mh_data_stream *stream, size_t offset,
	const void *bytes, size_t size, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	status = validate_write(stream, bytes, size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (offset > stream->size || size > stream->size - offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"private stream patch exceeds written bytes");

	directory = stream->directory;
	status = directory->ops->streams->write_at(directory->context,
		stream->token, offset, bytes == NULL ? "" : bytes, size,
		error_out);

	if (status != MH_ERROR_NONE)
		stream->state = STREAM_FAILED;

	return status;
}

size_t
mh_data_stream_size(const struct mh_data_stream *stream)
{
	return stream == NULL ? 0 : stream->size;
}

enum mh_error_code
mh_data_stream_seal(struct mh_data_stream *stream, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (stream == NULL || stream->state != STREAM_WRITING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stream cannot be sealed");

	directory = stream->directory;
	status = directory->ops->streams->seal(
		directory->context, stream->token, stream->size, error_out);
	stream->state = status == MH_ERROR_NONE ? STREAM_SEALED : STREAM_FAILED;

	return status;
}

enum mh_error_code
mh_data_stream_create(struct mh_data_stream *stream, const char *name,
	mh_data_publish_check check, void *context,
	struct mh_data_file **output, struct mh_error *error_out)
{
	struct mh_data_file *file;
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (stream == NULL || !mh_data_name_valid(name) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid private stream publication");

	if (stream->state != STREAM_SEALED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stream is not sealed");

	file = malloc(sizeof(*file));

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory publishing private stream");

	directory = stream->directory;
	status = directory->ops->streams->create(directory->context,
		stream->token, name, check, context, directory, &file->token,
		error_out);

	if (status != MH_ERROR_NONE) {
		free(file);
		return status;
	}

	file->directory = directory;
	stream->state = STREAM_PUBLISHED;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_data_stream_replace(struct mh_data_stream *stream, struct mh_data_file *file,
	mh_data_publish_check check, void *context, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (stream == NULL || file == NULL ||
		stream->directory != file->directory)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"private stream and target have different directories");

	if (stream->state != STREAM_SEALED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stream is not sealed");

	directory = stream->directory;
	status = directory->ops->streams->replace(directory->context,
		stream->token, file->token, check, context, directory,
		error_out);

	if (status == MH_ERROR_NONE)
		stream->state = STREAM_PUBLISHED;

	return status;
}

void
mh_data_stream_close(struct mh_data_stream *stream)
{
	struct mh_data_directory *directory;

	if (stream == NULL)
		return;

	directory = stream->directory;
	directory->ops->streams->close(directory->context, stream->token);
	free(stream);
}
