#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backends/media/ffmpeg_output.h"

#define OUTPUT_BUFFER_SIZE 32768

struct mh_ffmpeg_output {
	AVFormatContext	*format;
	AVIOContext	*io;
	unsigned char	*bytes;
	size_t		size, position, capacity;
};

static int
#if LIBAVFORMAT_VERSION_MAJOR < 61
write_bytes(void *context, unsigned char *bytes, int size)
#else
write_bytes(void *context, const unsigned char *bytes, int size)
#endif
{
	struct mh_ffmpeg_output *output;
	unsigned char *next;
	size_t needed, capacity;

	output = context;

	if (size < 0 ||
		(size_t)size > MH_RESOURCE_READ_LIMIT - output->position)
		return AVERROR(EFBIG);

	needed = output->position + (size_t)size;

	if (needed > output->capacity) {
		capacity = output->capacity == 0 ? OUTPUT_BUFFER_SIZE
						: output->capacity * 2;

		if (capacity < needed)
			capacity = needed;

		if (capacity > MH_RESOURCE_READ_LIMIT)
			capacity = MH_RESOURCE_READ_LIMIT;

		next = realloc(output->bytes, capacity);

		if (next == NULL)
			return AVERROR(ENOMEM);

		output->bytes = next;
		output->capacity = capacity;
	}

	if (size != 0)
		memcpy(output->bytes + output->position, bytes, (size_t)size);

	output->position = needed;

	if (output->size < needed)
		output->size = needed;

	return size;
}

static int64_t
seek_bytes(void *context, int64_t offset, int whence)
{
	struct mh_ffmpeg_output *output;
	int64_t base;

	output = context;

	if (whence == AVSEEK_SIZE)
		return (int64_t)output->size;

	whence &= ~AVSEEK_FORCE;

	if (whence == SEEK_SET)
		base = 0;
	else if (whence == SEEK_CUR)
		base = (int64_t)output->position;
	else if (whence == SEEK_END)
		base = (int64_t)output->size;
	else
		return AVERROR(EINVAL);

	if (offset < -base || offset > (int64_t)output->size - base)
		return AVERROR(EINVAL);

	output->position = (size_t)(base + offset);

	return (int64_t)output->position;
}

void
mh_ffmpeg_output_close(struct mh_ffmpeg_output *output)
{
	if (output == NULL)
		return;

	avformat_free_context(output->format);

	if (output->io != NULL)
		av_freep(&output->io->buffer);

	avio_context_free(&output->io);
	free(output->bytes);
	free(output);
}

enum mh_error_code
mh_ffmpeg_output_open(const char *container, struct mh_ffmpeg_output **result,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_output *output;
	unsigned char *buffer;

	output = calloc(1, sizeof(*output));

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media output");

	if (avformat_alloc_output_context2(
		&output->format, NULL, container, NULL) < 0) {
		mh_ffmpeg_output_close(output);
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without the requested media muxer");
	}

	buffer = av_malloc(OUTPUT_BUFFER_SIZE);

	if (buffer != NULL)
		output->io = avio_alloc_context(buffer, OUTPUT_BUFFER_SIZE, 1,
			output, NULL, write_bytes, seek_bytes);

	if (output->io == NULL) {
		av_free(buffer);
		mh_ffmpeg_output_close(output);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate media output buffer");
	}

	output->format->pb = output->io;
	output->format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_BITEXACT;
	*result = output;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

AVFormatContext *
mh_ffmpeg_output_format(struct mh_ffmpeg_output *output)
{
	return output->format;
}

enum mh_error_code
mh_ffmpeg_output_start(
	struct mh_ffmpeg_output *output, struct mh_error *error_out)
{
	if (avformat_write_header(output->format, NULL) < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot write media header");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_output_frame(struct mh_ffmpeg_output *output, AVCodecContext *codec,
	AVStream *stream, const AVFrame *frame, struct mh_error *error_out)
{
	AVPacket *packet;
	int result;

	packet = av_packet_alloc();

	if (packet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate encoded media packet");

	result = avcodec_send_frame(codec, frame);

	if (result < 0) {
		av_packet_free(&packet);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot submit media frame for encoding");
	}

	while ((result = avcodec_receive_packet(codec, packet)) >= 0) {
		av_packet_rescale_ts(
			packet, codec->time_base, stream->time_base);
		packet->stream_index = stream->index;
		result = av_interleaved_write_frame(output->format, packet);
		av_packet_unref(packet);

		if (result < 0)
			break;
	}

	av_packet_free(&packet);

	if (result != (frame == NULL ? AVERROR_EOF : AVERROR(EAGAIN)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot encode or write media packet");

	if (output->io->error < 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot buffer encoded media");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_output_finish(struct mh_ffmpeg_output *output, struct mh_file *file,
	struct mh_error *error_out)
{
	if (av_write_trailer(output->format) < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot finalize media container");

	avio_flush(output->io);

	if (output->io->error < 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot buffer completed media");

	file->data = output->bytes;
	file->size = output->size;
	output->bytes = NULL;
	output->size = output->position = output->capacity = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
