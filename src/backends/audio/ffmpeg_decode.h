#ifndef MH_AUDIO_FFMPEG_DECODE_H
#define MH_AUDIO_FFMPEG_DECODE_H

#include "backends/media/ffmpeg_input.h"
#include "interfaces/audio/decoder.h"

enum mh_error_code	mh_ffmpeg_opus_info(struct mh_ffmpeg_input *input,
	struct mh_audio_decode_info *output, struct mh_error *error_out);

/* Decode exactly info->frames into owned little-endian PCM. */
enum mh_error_code	mh_ffmpeg_opus_decode(struct mh_ffmpeg_input *input,
	const struct mh_audio_decode_info *info, unsigned char **output,
	struct mh_error *error_out);

#endif
