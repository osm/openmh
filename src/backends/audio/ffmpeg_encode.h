#ifndef MH_AUDIO_FFMPEG_ENCODE_H
#define MH_AUDIO_FFMPEG_ENCODE_H

#include "engine/core/file.h"
#include "interfaces/audio/decoder.h"
#include "interfaces/media/encode.h"

/* Whether the linked FFmpeg provides both libopus encoding and Ogg muxing. */
int	mh_audio_ffmpeg_encoding_available(void);

/*
 * Encode from frame zero to EOF as Ogg Opus, preserving channels, at 80 kbit/s
 * VBR per channel (160 kbit/s for stereo music).
 * The borrowed decoder advances. Output owns malloc bytes on success
 * and is unchanged on failure. Does not access the filesystem or open audio
 * devices.
 */
enum mh_error_code	mh_audio_ffmpeg_encode(struct mh_audio_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out);

#endif
