#ifndef MH_FFMPEG_AUDIO_STREAM_H
#define MH_FFMPEG_AUDIO_STREAM_H

#include "backends/media/ffmpeg_sdk.h"
#include "engine/core/file.h"
#include "interfaces/audio/decoder.h"

/*
 * Shared only by FFmpeg backends: decode supported audio tracks from packets.
 */
struct mh_ffmpeg_audio;
enum mh_error_code	mh_ffmpeg_audio_open(AVStream *stream,
	size_t byte_limit, struct mh_ffmpeg_audio **output,
	struct mh_error *error_out);
enum mh_error_code	mh_ffmpeg_audio_packet(struct mh_ffmpeg_audio *audio,
	const AVPacket *packet, struct mh_error *error_out);

/* Flush once, transfer owned S16LE PCM; output arguments unchanged on error. */
enum mh_error_code	mh_ffmpeg_audio_finish(struct mh_ffmpeg_audio *audio,
	struct mh_file *output, struct mh_audio_decode_info *info,
	struct mh_error *error_out);
void			mh_ffmpeg_audio_close(struct mh_ffmpeg_audio *audio);

size_t	mh_ffmpeg_audio_frames(const struct mh_ffmpeg_audio *state);

#endif
