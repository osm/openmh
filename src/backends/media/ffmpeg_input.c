#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backends/media/ffmpeg_input.h"

#define INPUT_BUFFER_BYTES 32768

static int
read_input(void *context, unsigned char *output, int size)
{
	struct mh_ffmpeg_input *input;

	input = context;

	if (size <= 0)
		return AVERROR(EINVAL);

	if ((size_t)size > input->size - input->offset)
		size = (int)(input->size - input->offset);

	if (size == 0)
		return AVERROR_EOF;

	if (input->resources != NULL) {
		input->read_status =
			mh_resource_source_read_at(input->resources,
				input->path, (unsigned long)input->offset,
				output, (size_t)size, NULL);

		if (input->read_status != MH_ERROR_NONE)
			return AVERROR(EIO);
	} else
		memcpy(output, input->data + input->offset, (size_t)size);

	input->offset += (size_t)size;

	return size;
}

static int64_t
seek_input(void *context, int64_t offset, int whence)
{
	struct mh_ffmpeg_input *input;
	size_t base;

	input = context;

	if (whence == AVSEEK_SIZE)
		return (int64_t)input->size;

	whence &= ~AVSEEK_FORCE;

	if (whence == SEEK_SET)
		base = 0;
	else if (whence == SEEK_CUR)
		base = input->offset;
	else if (whence == SEEK_END)
		base = input->size;
	else
		return AVERROR(EINVAL);

	if (offset < -(int64_t)base || offset > (int64_t)(input->size - base))
		return AVERROR(EINVAL);

	input->offset = (size_t)((int64_t)base + offset);

	return (int64_t)input->offset;
}

static int
reject_external_io(AVFormatContext *format, AVIOContext **io, const char *url,
	int flags, AVDictionary **options)
{
	(void)format;

	(void)io;
	(void)url;
	(void)flags;
	(void)options;

	return AVERROR(EPERM);
}

void
mh_ffmpeg_input_close(struct mh_ffmpeg_input *input)
{
	if (input == NULL)
		return;

	avformat_close_input(&input->format);

	if (input->io != NULL)
		av_freep(&input->io->buffer);

	avio_context_free(&input->io);
	free(input->data);
	free(input->path);
	free(input);
}

static enum mh_error_code
open_format(struct mh_ffmpeg_input *input, const char *format,
	const char *codecs, struct mh_error *error_out)
{
	const AVInputFormat *demuxer;
	AVDictionary *options;
	unsigned char *buffer;
	int result;

	demuxer = av_find_input_format(format);

	if (demuxer == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without the requested media format");

	buffer = av_malloc(INPUT_BUFFER_BYTES);

	if (buffer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media input buffer");

	input->io = avio_alloc_context(buffer, INPUT_BUFFER_BYTES, 0, input,
		read_input, NULL, seek_input);

	if (input->io == NULL) {
		av_free(buffer);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media input context");
	}

	input->format = avformat_alloc_context();

	if (input->format == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media format context");

	input->format->pb = input->io;
	input->format->flags |= AVFMT_FLAG_CUSTOM_IO;
	input->format->io_open = reject_external_io;
	input->format->max_streams = 2;
	options = NULL;
	result = av_dict_set(&options, "codec_whitelist", codecs, 0);

	if (result >= 0)
		result = avformat_open_input(
			&input->format, NULL, demuxer, &options);

	av_dict_free(&options);

	if (result >= 0)
		result = avformat_find_stream_info(input->format, NULL);

	if (input->read_status != MH_ERROR_NONE)
		return mh_error_set(error_out, input->read_status,
			"cannot read media resource");

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot read media container metadata");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_input_resource(const struct mh_resource_source *resources,
	const char *path, const char *format, const char *codecs,
	struct mh_ffmpeg_input **output, struct mh_error *error_out)
{
	struct mh_ffmpeg_input *input;
	unsigned long size;
	enum mh_error_code status;

	status = mh_resource_source_size(resources, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > MH_RESOURCE_READ_LIMIT ||
		(unsigned long)(size_t)size != size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"media resource exceeds size limit");

	input = calloc(1, sizeof(*input));

	if (input == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media input");

	input->size = (size_t)size;
	input->resources = resources;
	input->path = malloc(strlen(path) + 1);

	if (input->path == NULL) {
		mh_ffmpeg_input_close(input);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media path");
	}

	strcpy(input->path, path);
	status = open_format(input, format, codecs, error_out);

	if (status != MH_ERROR_NONE) {
		mh_ffmpeg_input_close(input);
		return status;
	}

	*output = input;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_input_memory(const unsigned char *data, size_t size,
	const char *format, const char *codecs, struct mh_ffmpeg_input **output,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_input *input;
	enum mh_error_code status;

	if (size == 0 || size > MH_RESOURCE_READ_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid media payload size");

	input = calloc(1, sizeof(*input));

	if (input == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media input");

	input->data = malloc(size);

	if (input->data == NULL) {
		mh_ffmpeg_input_close(input);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media payload");
	}

	memcpy(input->data, data, size);
	input->size = size;
	status = open_format(input, format, codecs, error_out);

	if (status != MH_ERROR_NONE) {
		mh_ffmpeg_input_close(input);
		return status;
	}

	*output = input;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
