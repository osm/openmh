#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_stream.h"

#define AUDIO_FRAME_SAMPLES_MAX 65536

struct mh_ffmpeg_audio {
	AVCodecContext	*codec;
	AVFrame		*frame;
	SwrContext	*conversion;
	unsigned char	*pcm;
	size_t		frames, capacity, limit;
	unsigned int	channels;
	int		rate;
	AVRational	time_base;
};

void
mh_ffmpeg_audio_close(struct mh_ffmpeg_audio *state)
{
	if (state == NULL)
		return;

	swr_free(&state->conversion);
	av_frame_free(&state->frame);
	avcodec_free_context(&state->codec);
	free(state->pcm);
	free(state);
}

static enum mh_error_code
open_state(struct mh_ffmpeg_audio *state, AVStream *stream, size_t limit,
	struct mh_error *error_out)
{
	const AVCodec *codec;
	AVCodecParameters *parameters;
	int result;

	parameters = stream->codecpar;

	if ((parameters->codec_id != AV_CODEC_ID_OPUS &&
		parameters->codec_id != AV_CODEC_ID_SMACKAUDIO &&
		parameters->codec_id != AV_CODEC_ID_PCM_U8 &&
		parameters->codec_id != AV_CODEC_ID_PCM_S16LE) ||
		parameters->sample_rate < 8000 ||
		parameters->sample_rate > 96000 ||
		(parameters->ch_layout.nb_channels != 1 &&
			parameters->ch_layout.nb_channels != 2))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected mono or stereo audio");

	codec = avcodec_find_decoder(parameters->codec_id);

	if (codec == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without audio decoding");

	state->codec = avcodec_alloc_context3(codec);
	state->frame = av_frame_alloc();

	if (state->codec == NULL || state->frame == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate audio decoding state");

	state->rate = parameters->sample_rate;
	state->channels = (unsigned int)parameters->ch_layout.nb_channels;
	state->limit = limit / (state->channels * 2);
	result = avcodec_parameters_to_context(state->codec, parameters);
	state->codec->err_recognition = AV_EF_CRCCHECK | AV_EF_EXPLODE;
	state->codec->thread_count = 1;
	state->codec->pkt_timebase = stream->time_base;
	state->time_base = stream->time_base;

	if (result >= 0)
		result = avcodec_open2(state->codec, codec, NULL);

	if (result >= 0)
		result = swr_alloc_set_opts2(&state->conversion,
			&state->codec->ch_layout, AV_SAMPLE_FMT_S16,
			state->rate, &state->codec->ch_layout,
			state->codec->sample_fmt, state->rate, 0, NULL);

	if (result >= 0)
		result = swr_init(state->conversion);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot initialize audio decoding");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
reserve_frames(struct mh_ffmpeg_audio *state, size_t frames,
	struct mh_error *error_out)
{
	unsigned char *next;
	size_t capacity, needed;

	if (frames > state->limit - state->frames)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded audio exceeds its size limit");

	needed = state->frames + frames;

	if (needed <= state->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	capacity = state->capacity == 0 ? (size_t)state->rate
					: state->capacity * 2;

	if (capacity < needed)
		capacity = needed;

	if (capacity > state->limit)
		capacity = state->limit;

	next = realloc(state->pcm, capacity * state->channels * 2);

	if (next == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot grow decoded audio");

	state->pcm = next;
	state->capacity = capacity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_frame(struct mh_ffmpeg_audio *state, struct mh_error *error_out)
{
	int16_t *samples;
	unsigned char *output, *destination;
	unsigned int value;
	size_t i, count;
	int frames;
	enum mh_error_code status;
	double timestamp, expected, tolerance;

	if (state->frame->nb_samples < 0 ||
		state->frame->nb_samples > AUDIO_FRAME_SAMPLES_MAX ||
		state->frame->sample_rate != state->rate ||
		state->frame->ch_layout.nb_channels != (int)state->channels ||
		state->frame->format != state->codec->sample_fmt)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio sample format changed during playback");

	if (state->frame->pts == mh_ffmpeg_no_timestamp ||
		state->time_base.num <= 0 || state->time_base.den <= 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"audio has no usable sample timeline");

	timestamp = state->frame->pts * av_q2d(state->time_base);
	expected = (double)state->frames / state->rate;
	tolerance = av_q2d(state->time_base) + 0.000000001;

	if (fabs(timestamp - expected) > tolerance)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio must start at zero and have a continuous sample timeline");

	if (state->frame->nb_samples == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = reserve_frames(
		state, (size_t)state->frame->nb_samples, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	samples = av_malloc_array((size_t)state->frame->nb_samples,
		state->channels * sizeof(*samples));

	if (samples == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate decoded audio samples");

	output = (unsigned char *)samples;
	frames = swr_convert(state->conversion, &output,
		state->frame->nb_samples,
		(const unsigned char **)state->frame->extended_data,
		state->frame->nb_samples);

	if (frames != state->frame->nb_samples) {
		av_free(samples);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"decoded audio sample count changed");
	}

	count = (size_t)frames * state->channels;
	destination = state->pcm + state->frames * state->channels * 2;

	for (i = 0; i < count; i++) {
		value = (unsigned int)samples[i] & 65535U;
		destination[i * 2] = (unsigned char)(value & 255U);
		destination[i * 2 + 1] = (unsigned char)(value >> 8);
	}

	av_free(samples);
	state->frames += (size_t)frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_packet(struct mh_ffmpeg_audio *state, const AVPacket *packet,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int result;

	result = avcodec_send_packet(state->codec, packet);

	if (result < 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid audio packet");

	while ((result = avcodec_receive_frame(state->codec, state->frame)) >=
		0) {
		status = append_frame(state, error_out);
		av_frame_unref(state->frame);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot decode audio packet");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_open(AVStream *stream, size_t byte_limit,
	struct mh_ffmpeg_audio **output, struct mh_error *error_out)
{
	struct mh_ffmpeg_audio *state;
	enum mh_error_code status;

	state = calloc(1, sizeof(*state));

	if (state == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate audio stream");

	status = open_state(state, stream, byte_limit, error_out);

	if (status != MH_ERROR_NONE) {
		mh_ffmpeg_audio_close(state);
		return status;
	}

	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_finish(struct mh_ffmpeg_audio *state, struct mh_file *output,
	struct mh_audio_decode_info *info, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_ffmpeg_audio_packet(state, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (swr_get_delay(state->conversion, state->rate) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected buffered audio samples");

	info->frames = state->frames;
	info->rate = state->rate;
	info->channels = state->channels;
	info->bits = 16;
	output->data = state->pcm;
	output->size = state->frames * state->channels * 2;
	state->pcm = NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

size_t
mh_ffmpeg_audio_frames(const struct mh_ffmpeg_audio *state)
{
	return state == NULL ? 0 : state->frames;
}
