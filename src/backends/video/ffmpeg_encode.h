#ifndef MH_VIDEO_FFMPEG_ENCODE_H
#define MH_VIDEO_FFMPEG_ENCODE_H

#include "engine/core/file.h"
#include "interfaces/media/encode.h"
#include "interfaces/video/decoder.h"

int	mh_video_ffmpeg_encoding_available(void);

/*
 * Borrow a fresh decoder opened with audio enabled. Encode its complete clip
 * as VP9/Opus WebM, retaining dimensions, cadence and frame count. Output owns
 * malloc bytes on success and is unchanged on failure. Decoder advances.
 */
enum mh_error_code	mh_video_ffmpeg_encode(struct mh_video_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out);

#endif
