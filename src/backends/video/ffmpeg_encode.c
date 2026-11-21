#include <string.h>

#include "backends/video/ffmpeg_encode_internal.h"

int
mh_video_ffmpeg_encoding_available(void)
{
	return avcodec_find_encoder_by_name("libvpx-vp9") != NULL &&
		avcodec_find_encoder_by_name("libopus") != NULL &&
		av_bsf_get_by_name("vp9_superframe") != NULL &&
		av_guess_format("webm", NULL, NULL) != NULL;
}

static void
close_encoder(struct mh_webm_encoder *encoder)
{
	mh_ffmpeg_audio_encoder_close(encoder->audio);
	av_frame_free(&encoder->frame);
	avcodec_free_context(&encoder->codec);
	sws_freeContext(encoder->conversion);
	mh_ffmpeg_output_close(encoder->mux);
}

static enum mh_error_code
open_encoder(struct mh_webm_encoder *encoder, struct mh_error *error_out)
{
	struct mh_audio_decode_info audio;
	enum mh_error_code status;

	status = mh_ffmpeg_output_open("webm", &encoder->mux, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webm_encoder_picture_open(encoder, error_out);

	if (status == MH_ERROR_NONE && encoder->info.audio_rate != 0) {
		memset(&audio, 0, sizeof(audio));
		audio.rate = encoder->info.audio_rate;
		audio.channels = encoder->info.audio_channels;
		audio.bits = encoder->info.audio_bits;
		status = mh_ffmpeg_audio_encoder_open(
			encoder->mux, &audio, 2, &encoder->audio, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_start(encoder->mux, error_out);

	return status;
}

static enum mh_error_code
encode_frames(struct mh_webm_encoder *encoder, struct mh_video_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_error *error_out)
{
	struct mh_video_frame frame;
	size_t frame_bytes;
	int available;
	enum mh_error_code status;

	for (;;) {
		if (check != NULL) {
			status = check(context, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_video_decoder_read(
			decoder, &frame, &available, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!available)
			break;

		status = mh_webm_encoder_picture(encoder, &frame, error_out);

		if (status == MH_ERROR_NONE && frame.pcm_bytes != 0) {
			frame_bytes = encoder->info.audio_channels *
				(encoder->info.audio_bits / 8);
			status = mh_ffmpeg_audio_encoder_write(encoder->audio,
				frame.pcm, frame.pcm_bytes / frame_bytes,
				error_out);
		}

		if (status != MH_ERROR_NONE)
			return status;

		encoder->position++;
	}

	if (encoder->position != encoder->info.frames)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"movie encoder requires a decoder positioned at frame zero");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
finish_encoder(struct mh_webm_encoder *encoder, struct mh_file *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_ffmpeg_output_frame(
		encoder->mux, encoder->codec, encoder->stream, NULL, error_out);

	if (status == MH_ERROR_NONE && encoder->audio != NULL)
		status = mh_ffmpeg_audio_encoder_finish(
			encoder->audio, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_finish(
			encoder->mux, output, error_out);

	return status;
}

enum mh_error_code
mh_video_ffmpeg_encode(struct mh_video_decoder *decoder,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_webm_encoder encoder;
	const struct mh_video_decode_info *info;
	enum mh_error_code status;

	info = mh_video_decoder_info(decoder);

	if (info == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing movie input or output");

	if (info->width > MH_WEBM_WIDTH_LIMIT ||
		info->height > MH_WEBM_HEIGHT_LIMIT ||
		info->frames > MH_WEBM_FRAME_LIMIT ||
		!(info->interval_us >= 1000.0 &&
			info->interval_us <= 1000000.0))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie exceeds WebM playback limits");

	memset(&encoder, 0, sizeof(encoder));
	encoder.info = *info;
	status = open_encoder(&encoder, error_out);

	if (status == MH_ERROR_NONE)
		status = encode_frames(
			&encoder, decoder, check, context, error_out);

	if (status == MH_ERROR_NONE)
		status = finish_encoder(&encoder, output, error_out);

	close_encoder(&encoder);

	return status;
}
