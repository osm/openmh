#include "backends/audio/ffmpeg_encode.h"
#include "backends/audio/ffmpeg_encode_stream.h"

#define INPUT_FRAMES 4096

int
mh_audio_ffmpeg_encoding_available(void)
{
	return avcodec_find_encoder_by_name("libopus") != NULL &&
		av_guess_format("ogg", NULL, NULL) != NULL;
}

static enum mh_error_code
encode_samples(struct mh_audio_decoder *decoder,
	struct mh_ffmpeg_audio_encoder *encoder, mh_media_encode_check check,
	void *context, struct mh_error *error_out)
{
	unsigned char pcm[INPUT_FRAMES * 4];
	size_t count, total;
	enum mh_error_code status;

	total = 0;

	for (;;) {
		if (check != NULL) {
			status = check(context, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_audio_decoder_read(decoder, pcm, sizeof(pcm),
			INPUT_FRAMES, &count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (count == 0)
			break;

		status = mh_ffmpeg_audio_encoder_write(
			encoder, pcm, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		total += count;
	}

	if (total != mh_audio_decoder_info(decoder)->frames)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"PCM duration changed during encoding");

	return mh_ffmpeg_audio_encoder_finish(encoder, error_out);
}

enum mh_error_code
mh_audio_ffmpeg_encode(struct mh_audio_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_output *mux;
	struct mh_ffmpeg_audio_encoder *encoder;
	const struct mh_audio_decode_info *info;
	enum mh_error_code status;

	info = mh_audio_decoder_info(decoder);

	if (info == NULL || info->frames == 0 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing PCM input or encoded output");

	mux = NULL;
	encoder = NULL;
	status = mh_audio_decoder_seek(decoder, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_open("ogg", &mux, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_audio_encoder_open(
			mux, info, info->channels, &encoder, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_start(mux, error_out);

	if (status == MH_ERROR_NONE)
		status = encode_samples(
			decoder, encoder, check, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_finish(mux, output, error_out);

	mh_ffmpeg_audio_encoder_close(encoder);
	mh_ffmpeg_output_close(mux);

	return status;
}
