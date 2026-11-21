#include <stdlib.h>
#include <string.h>

#include "backends/audio/ffmpeg_decode.h"
#include "backends/audio/ffmpeg_stream.h"
#include "engine/audio/sample.h"

#define OPUS_SAMPLE_RATE 48000

enum mh_error_code
mh_ffmpeg_opus_info(struct mh_ffmpeg_input *input,
	struct mh_audio_decode_info *output, struct mh_error *error_out)
{
	AVStream *stream;
	AVCodecParameters *parameters;
	struct mh_audio_decode_info info;
	int64_t frames;

	if (input->format->nb_streams != 1)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected a single Ogg Opus audio stream");

	stream = input->format->streams[0];
	parameters = stream->codecpar;

	if (parameters->codec_id != AV_CODEC_ID_OPUS ||
		parameters->sample_rate != OPUS_SAMPLE_RATE ||
		(parameters->ch_layout.nb_channels != 1 &&
			parameters->ch_layout.nb_channels != 2))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected mono or stereo Ogg Opus audio");

	if (stream->duration == mh_ffmpeg_no_timestamp ||
		stream->duration < 0 || stream->time_base.num != 1 ||
		stream->time_base.den != OPUS_SAMPLE_RATE ||
		parameters->initial_padding < 0 ||
		stream->duration < parameters->initial_padding)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ogg Opus duration");

	/* Ogg's final granule includes the encoder's initial pre-skip. */
	frames = stream->duration - parameters->initial_padding;
	info.channels = (unsigned int)parameters->ch_layout.nb_channels;

	if ((long double)frames >
		(long double)(MH_AUDIO_SAMPLE_BYTES_MAX / (info.channels * 2)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded Opus audio exceeds sample size limit");

	info.frames = (size_t)frames;
	info.rate = OPUS_SAMPLE_RATE;
	info.bits = 16;
	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_stream(struct mh_ffmpeg_audio *audio, struct mh_ffmpeg_input *input,
	struct mh_error *error_out)
{
	AVPacket *packet;
	enum mh_error_code status;
	int result;

	packet = av_packet_alloc();

	if (packet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Opus audio packet");

	status = MH_ERROR_NONE;

	while ((result = av_read_frame(input->format, packet)) >= 0) {
		status = mh_ffmpeg_audio_packet(audio, packet, error_out);
		av_packet_unref(packet);

		if (status != MH_ERROR_NONE)
			break;
	}

	av_packet_free(&packet);

	if (status != MH_ERROR_NONE)
		return status;

	if (input->read_status != MH_ERROR_NONE)
		return mh_error_set(error_out, input->read_status,
			"cannot read Opus resource");

	if (result != AVERROR_EOF)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ogg Opus packet stream");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_opus_decode(struct mh_ffmpeg_input *input,
	const struct mh_audio_decode_info *info, unsigned char **output,
	struct mh_error *error_out)
{
	struct mh_ffmpeg_audio *audio;
	struct mh_audio_decode_info decoded;
	struct mh_file pcm;
	enum mh_error_code status;

	audio = NULL;
	memset(&pcm, 0, sizeof(pcm));
	status = mh_ffmpeg_audio_open(input->format->streams[0],
		info->frames * info->channels * 2, &audio, error_out);

	if (status == MH_ERROR_NONE)
		status = decode_stream(audio, input, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_ffmpeg_audio_finish(
			audio, &pcm, &decoded, error_out);

	if (status == MH_ERROR_NONE && decoded.frames != info->frames)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"Opus decoded duration differs from its metadata");

	if (status == MH_ERROR_NONE)
		*output = pcm.data;
	else
		free(pcm.data);
	mh_ffmpeg_audio_close(audio);

	return status;
}
