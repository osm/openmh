#ifndef MH_FFMPEG_INPUT_H
#define MH_FFMPEG_INPUT_H

#include "backends/media/ffmpeg_sdk.h"
#include "interfaces/storage/resource.h"

struct mh_ffmpeg_input {
	const struct mh_resource_source	*resources;
	char				*path;
	unsigned char			*data;
	size_t				size, offset;
	AVIOContext			*io;
	AVFormatContext			*format;
	enum mh_error_code		read_status;
};

/* Owned input; resources remain borrowed, memory bytes are copied. */
enum mh_error_code	mh_ffmpeg_input_resource(
	const struct mh_resource_source *resources, const char *path,
	const char *format, const char *codecs, struct mh_ffmpeg_input **output,
	struct mh_error *error_out);
enum mh_error_code	mh_ffmpeg_input_memory(const unsigned char *data,
	size_t size, const char *format, const char *codecs,
	struct mh_ffmpeg_input **output, struct mh_error *error_out);
void			mh_ffmpeg_input_close(struct mh_ffmpeg_input *input);

#endif
