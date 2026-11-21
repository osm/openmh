#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_encode_stream.h"
#include "engine/audio/sample.h"

#define ENCODE_INPUT_FRAMES 4096
#define ENCODE_OPUS_RATE 48000
#define ENCODE_OPUS_CHANNEL_BITRATE 80000

struct mh_ffmpeg_audio_encoder {
	struct mh_ffmpeg_output		*mux;
	struct mh_audio_decode_info	input;
	AVCodecContext			*codec;
	AVStream			*stream;
	AVFrame				*frame;
	SwrContext			*resampler;
	AVAudioFifo			*fifo;
	size_t				frames;
};

void
mh_ffmpeg_audio_encoder_close(struct mh_ffmpeg_audio_encoder *encoder)
{
	if (encoder == NULL)
		return;

	av_audio_fifo_free(encoder->fifo);
	swr_free(&encoder->resampler);
	av_frame_free(&encoder->frame);
	avcodec_free_context(&encoder->codec);
	free(encoder);
}

static enum mh_error_code
open_codec(struct mh_ffmpeg_audio_encoder *encoder, unsigned int channels,
	struct mh_error *error_out)
{
	AVFormatContext *format;
	const AVCodec *codec;
	int result;

	codec = avcodec_find_encoder_by_name("libopus");

	if (codec == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without the libopus encoder");

	format = mh_ffmpeg_output_format(encoder->mux);
	encoder->codec = avcodec_alloc_context3(codec);
	encoder->stream = avformat_new_stream(format, NULL);

	if (encoder->codec == NULL || encoder->stream == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Opus encoding state");

	encoder->codec->sample_fmt = AV_SAMPLE_FMT_FLT;
	encoder->codec->sample_rate = ENCODE_OPUS_RATE;
	encoder->codec->bit_rate = ENCODE_OPUS_CHANNEL_BITRATE * channels;
	encoder->codec->time_base.num = 1;
	encoder->codec->time_base.den = ENCODE_OPUS_RATE;
	av_channel_layout_default(&encoder->codec->ch_layout, (int)channels);

	if (format->oformat->flags & AVFMT_GLOBALHEADER)
		encoder->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	result = av_opt_set(encoder->codec->priv_data, "vbr", "on", 0);

	if (result >= 0)
		result = avcodec_open2(encoder->codec, codec, NULL);

	if (result >= 0)
		result = avcodec_parameters_from_context(
			encoder->stream->codecpar, encoder->codec);

	if (result < 0 || encoder->codec->frame_size <= 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot initialize Opus encoding");

	encoder->stream->time_base = encoder->codec->time_base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_resampler(
	struct mh_ffmpeg_audio_encoder *encoder, struct mh_error *error_out)
{
	AVChannelLayout layout;
	enum AVSampleFormat format;
	int result;

	av_channel_layout_default(&layout, (int)encoder->input.channels);
	format =
		encoder->input.bits == 8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;
	result = swr_alloc_set_opts2(&encoder->resampler,
		&encoder->codec->ch_layout, AV_SAMPLE_FMT_FLT, ENCODE_OPUS_RATE,
		&layout, format, (int)encoder->input.rate, 0, NULL);
	av_channel_layout_uninit(&layout);

	if (result >= 0)
		result = swr_init(encoder->resampler);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot initialize Opus resampling");

	encoder->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT,
		encoder->codec->ch_layout.nb_channels,
		encoder->codec->frame_size);
	encoder->frame = av_frame_alloc();

	if (encoder->fifo == NULL || encoder->frame == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Opus sample buffers");

	encoder->frame->format = AV_SAMPLE_FMT_FLT;
	encoder->frame->sample_rate = ENCODE_OPUS_RATE;
	encoder->frame->nb_samples = encoder->codec->frame_size;
	result = av_channel_layout_copy(
		&encoder->frame->ch_layout, &encoder->codec->ch_layout);

	if (result >= 0)
		result = av_frame_get_buffer(encoder->frame, 0);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Opus encoding frame");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_encoder_open(struct mh_ffmpeg_output *mux,
	const struct mh_audio_decode_info *input, unsigned int channels,
	struct mh_ffmpeg_audio_encoder **output, struct mh_error *error_out)
{
	struct mh_ffmpeg_audio_encoder *encoder;
	enum mh_error_code status;

	if (input->rate < 8000 || input->rate > 96000 ||
		(input->channels != 1 && input->channels != 2) ||
		(channels != 1 && channels != 2) ||
		(input->bits != 8 && input->bits != 16))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported PCM format for Opus conversion");

	encoder = calloc(1, sizeof(*encoder));

	if (encoder == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Opus encoder");

	encoder->mux = mux;
	encoder->input = *input;
	status = open_codec(encoder, channels, error_out);

	if (status == MH_ERROR_NONE)
		status = open_resampler(encoder, error_out);

	if (status != MH_ERROR_NONE) {
		mh_ffmpeg_audio_encoder_close(encoder);
		return status;
	}

	*output = encoder;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_ready(struct mh_ffmpeg_audio_encoder *encoder, int last,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int count;
	size_t limit;

	limit = MH_AUDIO_SAMPLE_BYTES_MAX /
		(encoder->codec->ch_layout.nb_channels * 2);

	while ((count = av_audio_fifo_size(encoder->fifo)) > 0 &&
		(last || count >= encoder->codec->frame_size)) {
		if (count > encoder->codec->frame_size)
			count = encoder->codec->frame_size;

		if ((size_t)count > limit - encoder->frames)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"converted audio exceeds the playback sample limit");

		if (av_frame_make_writable(encoder->frame) < 0)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot write Opus input frame");

		if (av_audio_fifo_read(encoder->fifo,
			(void **)encoder->frame->data, count) != count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"cannot read Opus sample buffer");

		encoder->frame->nb_samples = count;
		encoder->frame->pts = (int64_t)encoder->frames;
		status = mh_ffmpeg_output_frame(encoder->mux, encoder->codec,
			encoder->stream, encoder->frame, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		encoder->frames += (size_t)count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resample(struct mh_ffmpeg_audio_encoder *encoder, const unsigned char *pcm,
	int frames, int *converted, struct mh_error *error_out)
{
	unsigned char *samples;
	int capacity, count;

	capacity = swr_get_out_samples(encoder->resampler, frames);

	if (capacity < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot size resampled audio");

	if (capacity == 0) {
		*converted = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	samples = av_malloc_array((size_t)capacity,
		(size_t)encoder->codec->ch_layout.nb_channels * sizeof(float));

	if (samples == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate resampling buffer");

	count = swr_convert(encoder->resampler, &samples, capacity,
		pcm == NULL ? NULL : &pcm, frames);

	if (count >= 0 &&
		av_audio_fifo_write(encoder->fifo, (void **)&samples, count) !=
			count)
		count = -1;

	av_free(samples);

	if (count < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot resample PCM for Opus encoding");

	*converted = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_encoder_write(struct mh_ffmpeg_audio_encoder *encoder,
	const unsigned char *pcm, size_t frames, struct mh_error *error_out)
{
	int16_t native[ENCODE_INPUT_FRAMES * 2];
	const unsigned char *source;
	size_t count, i, bytes_per_frame;
	int value, converted;
	enum mh_error_code status;

	bytes_per_frame = encoder->input.channels * (encoder->input.bits / 8);

	while (frames != 0) {
		count = frames > ENCODE_INPUT_FRAMES ? ENCODE_INPUT_FRAMES
						: frames;
		source = pcm;

		if (encoder->input.bits == 16) {
			for (i = 0; i < count * encoder->input.channels; i++) {
				value = pcm[i * 2] | ((int)pcm[i * 2 + 1] << 8);
				native[i] =
					(int16_t)(value >= 32768 ? value - 65536
								: value);
			}

			source = (const unsigned char *)native;
		}

		status = resample(
			encoder, source, (int)count, &converted, error_out);

		if (status == MH_ERROR_NONE)
			status = encode_ready(encoder, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		pcm += count * bytes_per_frame;
		frames -= count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_audio_encoder_finish(
	struct mh_ffmpeg_audio_encoder *encoder, struct mh_error *error_out)
{
	enum mh_error_code status;
	int converted;

	converted = 0;

	do {
		status = resample(encoder, NULL, 0, &converted, error_out);

		if (status == MH_ERROR_NONE)
			status = encode_ready(encoder, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} while (converted != 0);

	status = encode_ready(encoder, 1, error_out);

	if (status == MH_ERROR_NONE && encoder->frames == 0)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"PCM input is too short to resample");

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_output_frame(encoder->mux, encoder->codec,
			encoder->stream, NULL, error_out);

	return status;
}
