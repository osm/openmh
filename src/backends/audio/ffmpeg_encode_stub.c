#include "backends/audio/ffmpeg_encode.h"

int
mh_audio_ffmpeg_encoding_available(void)
{
	return 0;
}

enum mh_error_code
mh_audio_ffmpeg_encode(struct mh_audio_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out)
{
	(void)decoder;

	(void)check;
	(void)context;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"FFmpeg encoding support was not built");
}
