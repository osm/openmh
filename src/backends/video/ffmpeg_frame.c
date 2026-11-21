#include <errno.h>
#include <math.h>
#include <string.h>

#include "backends/video/ffmpeg_internal.h"

/*
 * Queue ahead without changing the audio timeline; playback starts at sample 0.
 */
#define AUDIO_PREFETCH_SECONDS 0.25

static enum mh_error_code
convert_palette(struct mh_ffmpeg_video *decoder, struct mh_error *error_out)
{
	const unsigned char *row;
	unsigned char *output;
	unsigned int x, y;
	uint32_t color;

	for (y = 0; y < decoder->info.height; y++) {
		row = decoder->frame->data[0] + y * decoder->frame->linesize[0];
		output = decoder->rgba + (size_t)y * decoder->info.width * 4;

		for (x = 0; x < decoder->info.width; x++) {
			/*
			 * FFmpeg's PAL8 palette holds native-endian ARGB words.
			 */
			memcpy(&color, decoder->frame->data[1] + row[x] * 4,
				sizeof(color));
			output[x * 4] = (unsigned char)(color >> 16);
			output[x * 4 + 1] = (unsigned char)(color >> 8);
			output[x * 4 + 2] = (unsigned char)color;
			output[x * 4 + 3] = 255;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
convert_picture(struct mh_ffmpeg_video *decoder, struct mh_error *error_out)
{
	AVFrame *frame;
	unsigned char *planes[4];
	int strides[4], matrix, full_range;
	const int *coefficients;

	frame = decoder->frame;

	if (frame->width != (int)decoder->info.width ||
		frame->height != (int)decoder->info.height ||
		frame->format !=
			(decoder->smacker ? AV_PIX_FMT_PAL8
					: AV_PIX_FMT_YUV420P))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unexpected movie pixel format or dimensions");

	if (decoder->smacker)
		return convert_palette(decoder, error_out);

	decoder->conversion = sws_getCachedContext(decoder->conversion,
		frame->width, frame->height, AV_PIX_FMT_YUV420P, frame->width,
		frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);

	if (decoder->conversion == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie color conversion");

	matrix = frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709
						: SWS_CS_ITU601;

	if (frame->colorspace != AVCOL_SPC_UNSPECIFIED &&
		frame->colorspace != AVCOL_SPC_BT709 &&
		frame->colorspace != AVCOL_SPC_BT470BG &&
		frame->colorspace != AVCOL_SPC_SMPTE170M)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported movie color matrix");

	full_range = frame->color_range == AVCOL_RANGE_JPEG;
	coefficients = sws_getCoefficients(matrix);

	if (sws_setColorspaceDetails(decoder->conversion, coefficients,
		full_range, coefficients, 1, 0, 1 << 16, 1 << 16) < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot configure movie color conversion");

	planes[0] = decoder->rgba;
	planes[1] = planes[2] = planes[3] = NULL;
	strides[0] = frame->width * 4;
	strides[1] = strides[2] = strides[3] = 0;

	if (sws_scale(decoder->conversion,
		(const unsigned char *const *)frame->data, frame->linesize,
		0, frame->height, planes, strides) != frame->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot convert movie picture");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
select_audio(struct mh_ffmpeg_video *decoder, struct mh_video_frame *frame)
{
	size_t end, total, frame_bytes;
	double until;

	frame->pcm = NULL;
	frame->pcm_bytes = 0;

	if (decoder->audio.size == 0)
		return;

	frame_bytes = decoder->info.audio_channels * 2;
	total = decoder->audio.size / frame_bytes;
	until = ((decoder->position + 1) * decoder->info.interval_us /
				1000000.0 +
			AUDIO_PREFETCH_SECONDS) *
		decoder->info.audio_rate;
	end = until >= total ? total : (size_t)floor(until + 0.5);

	if (decoder->smacker)
		end = decoder->packets[decoder->position].audio_end;

	if (decoder->position + 1 == decoder->count)
		end = total;

	frame->pcm =
		decoder->audio.data + decoder->audio_position * frame_bytes;
	frame->pcm_bytes = (end - decoder->audio_position) * frame_bytes;
	decoder->audio_position = end;
}

enum mh_error_code
mh_ffmpeg_video_read(struct mh_ffmpeg_video *decoder,
	struct mh_video_frame *output, struct mh_error *error_out)
{
	struct mh_video_frame frame;
	enum mh_error_code status;
	int result;

	result = avcodec_send_packet(
		decoder->codec, decoder->packets[decoder->position].packet);

	if (result >= 0)
		result = avcodec_receive_frame(decoder->codec, decoder->frame);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"movie packet does not contain a visible video frame");

	status = convert_picture(decoder, error_out);
	av_frame_unref(decoder->frame);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * The packer disables alternate reference frames: one packet, one
	 * picture.
	 */
	result = avcodec_receive_frame(decoder->codec, decoder->frame);

	if (result != AVERROR(EAGAIN))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie packet contains multiple video frames");

	frame.rgba = decoder->rgba;
	frame.rgba_bytes =
		(size_t)decoder->info.width * decoder->info.height * 4;
	select_audio(decoder, &frame);
	decoder->position++;
	*output = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
