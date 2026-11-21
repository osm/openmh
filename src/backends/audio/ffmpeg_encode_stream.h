#ifndef MH_AUDIO_FFMPEG_ENCODE_STREAM_H
#define MH_AUDIO_FFMPEG_ENCODE_STREAM_H

#include "backends/media/ffmpeg_output.h"
#include "interfaces/audio/decoder.h"

struct mh_ffmpeg_audio_encoder;

/* Private shared Opus track encoder for standalone music and movie muxing. */
enum mh_error_code	mh_ffmpeg_audio_encoder_open(
	struct mh_ffmpeg_output *mux, const struct mh_audio_decode_info *input,
	unsigned int channels, struct mh_ffmpeg_audio_encoder **output,
	struct mh_error *error_out);
enum mh_error_code	mh_ffmpeg_audio_encoder_write(
	struct mh_ffmpeg_audio_encoder *encoder, const unsigned char *pcm,
	size_t frames, struct mh_error *error_out);
enum mh_error_code	mh_ffmpeg_audio_encoder_finish(
	struct mh_ffmpeg_audio_encoder *encoder, struct mh_error *error_out);
void			mh_ffmpeg_audio_encoder_close(
	struct mh_ffmpeg_audio_encoder *encoder);

#endif
