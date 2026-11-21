#include "backends/video/ffmpeg_encode_internal.h"

static enum mh_error_code
open_codec(struct mh_webm_encoder *encoder, struct mh_error *error_out)
{
	const AVCodec *codec;
	AVFormatContext *format;
	AVCodecContext *state;
	int result;

	codec = avcodec_find_encoder_by_name("libvpx-vp9");

	if (codec == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without the libvpx VP9 encoder");

	format = mh_ffmpeg_output_format(encoder->mux);
	encoder->codec = avcodec_alloc_context3(codec);
	encoder->stream = avformat_new_stream(format, NULL);

	if (encoder->codec == NULL || encoder->stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate VP9 encoding state");

	state = encoder->codec;

	/* Keep library startup messages out of extraction progress. */
	state->log_level_offset = AV_LOG_VERBOSE - AV_LOG_INFO;
	state->width = (int)encoder->info.width;
	state->height = (int)encoder->info.height;
	state->pix_fmt = AV_PIX_FMT_YUV420P;
	state->time_base =
		av_d2q(encoder->info.interval_us / 1000000.0, 1000000);
	state->framerate = av_inv_q(state->time_base);
	state->bit_rate = 0;
	state->gop_size = 120;
	state->thread_count = 1;
	state->colorspace = AVCOL_SPC_SMPTE170M;
	state->color_range = AVCOL_RANGE_MPEG;

	if (format->oformat->flags & AVFMT_GLOBALHEADER)
		state->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	/*
	 * One visible picture per packet matches the original frame interface.
	 */
	result = av_opt_set_int(state->priv_data, "crf", 24, 0);

	if (result >= 0)
		result = av_opt_set_int(state->priv_data, "auto-alt-ref", 0, 0);

	if (result >= 0)
		result =
			av_opt_set_int(state->priv_data, "lag-in-frames", 0, 0);

	if (result >= 0)
		result = av_opt_set_int(state->priv_data, "cpu-used", 4, 0);

	if (result >= 0)
		result = av_opt_set(state->priv_data, "deadline", "good", 0);

	if (result >= 0)
		result = avcodec_open2(state, codec, NULL);

	if (result >= 0)
		result = avcodec_parameters_from_context(
			encoder->stream->codecpar, state);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot initialize VP9 encoding");

	encoder->stream->time_base = state->time_base;
	encoder->stream->avg_frame_rate = state->framerate;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_conversion(struct mh_webm_encoder *encoder, struct mh_error *error_out)
{
	const int *coefficients;
	int width, height;

	width = (int)encoder->info.width;
	height = (int)encoder->info.height;
	encoder->frame = av_frame_alloc();
	encoder->conversion =
		sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height,
			AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

	if (encoder->frame == NULL || encoder->conversion == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate VP9 picture conversion");

	encoder->frame->format = AV_PIX_FMT_YUV420P;
	encoder->frame->width = width;
	encoder->frame->height = height;
	encoder->frame->colorspace = AVCOL_SPC_SMPTE170M;
	encoder->frame->color_range = AVCOL_RANGE_MPEG;

	if (av_frame_get_buffer(encoder->frame, 0) < 0)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate VP9 input picture");

	coefficients = sws_getCoefficients(SWS_CS_ITU601);

	if (sws_setColorspaceDetails(encoder->conversion, coefficients, 1,
		coefficients, 0, 0, 1 << 16, 1 << 16) < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot configure VP9 color conversion");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webm_encoder_picture_open(
	struct mh_webm_encoder *encoder, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = open_codec(encoder, error_out);

	if (status == MH_ERROR_NONE)
		status = open_conversion(encoder, error_out);

	return status;
}

enum mh_error_code
mh_webm_encoder_picture(struct mh_webm_encoder *encoder,
	const struct mh_video_frame *frame, struct mh_error *error_out)
{
	const unsigned char *planes[4];
	int strides[4];

	if (av_frame_make_writable(encoder->frame) < 0)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot write VP9 input picture");

	planes[0] = frame->rgba;
	planes[1] = planes[2] = planes[3] = NULL;
	strides[0] = (int)encoder->info.width * 4;
	strides[1] = strides[2] = strides[3] = 0;

	if (sws_scale(encoder->conversion, planes, strides, 0,
		(int)encoder->info.height, encoder->frame->data,
		encoder->frame->linesize) != (int)encoder->info.height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot convert VP9 input picture");

	encoder->frame->pts = encoder->position;

	return mh_ffmpeg_output_frame(encoder->mux, encoder->codec,
		encoder->stream, encoder->frame, error_out);
}
