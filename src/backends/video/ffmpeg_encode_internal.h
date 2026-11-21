#ifndef MH_VIDEO_FFMPEG_ENCODE_INTERNAL_H
#define MH_VIDEO_FFMPEG_ENCODE_INTERNAL_H

#include "backends/audio/ffmpeg_encode_stream.h"
#include "backends/video/ffmpeg_encode.h"
#include "backends/video/ffmpeg_limits.h"

struct mh_webm_encoder {
	struct mh_ffmpeg_output		*mux;
	struct mh_ffmpeg_audio_encoder	*audio;
	struct mh_video_decode_info	info;
	AVCodecContext			*codec;
	AVStream			*stream;
	AVFrame				*frame;
	struct SwsContext		*conversion;
	unsigned long			position;
};

enum mh_error_code	mh_webm_encoder_picture_open(
	struct mh_webm_encoder *encoder, struct mh_error *error_out);
enum mh_error_code	mh_webm_encoder_picture(struct mh_webm_encoder *encoder,
	const struct mh_video_frame *frame, struct mh_error *error_out);

#endif
