#ifndef MH_FFMPEG_OUTPUT_H
#define MH_FFMPEG_OUTPUT_H

#include "backends/media/ffmpeg_sdk.h"
#include "interfaces/storage/resource.h"

struct mh_ffmpeg_output;

/* Bounded, owned memory output; no filesystem or protocol access. */
enum mh_error_code	mh_ffmpeg_output_open(const char *container,
	struct mh_ffmpeg_output **output, struct mh_error *error_out);
AVFormatContext	       *mh_ffmpeg_output_format(
	struct mh_ffmpeg_output *output);
enum mh_error_code	mh_ffmpeg_output_start(struct mh_ffmpeg_output *output,
	struct mh_error *error_out);

/* Send one frame (NULL to flush), drain packets and mux them immediately. */
enum mh_error_code	mh_ffmpeg_output_frame(struct mh_ffmpeg_output *output,
	AVCodecContext *codec, AVStream *stream, const AVFrame *frame,
	struct mh_error *error_out);

/* Transfers malloc-owned bytes on success; leaves file unchanged on error. */
enum mh_error_code	mh_ffmpeg_output_finish(struct mh_ffmpeg_output *output,
	struct mh_file *file, struct mh_error *error_out);
void			mh_ffmpeg_output_close(struct mh_ffmpeg_output *output);

#endif
