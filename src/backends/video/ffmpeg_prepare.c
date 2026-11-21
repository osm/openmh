#include <math.h>
#include <stdlib.h>

#include "backends/audio/ffmpeg_stream.h"
#include "backends/video/ffmpeg_internal.h"
#include "engine/audio/sample.h"

static int
is_audio_codec(enum AVCodecID codec, int smacker)
{
	if (!smacker)
		return codec == AV_CODEC_ID_OPUS;

	return codec == AV_CODEC_ID_SMACKAUDIO || codec == AV_CODEC_ID_PCM_U8 ||
		codec == AV_CODEC_ID_PCM_S16LE;
}

static enum mh_error_code
select_tracks(struct mh_ffmpeg_input *input, int smacker, int *video,
	int *audio, struct mh_error *error_out)
{
	AVCodecParameters *parameters;
	unsigned int i;

	*video = *audio = -1;

	for (i = 0; i < input->format->nb_streams; i++) {
		parameters = input->format->streams[i]->codecpar;

		if (parameters->codec_id ==
				(smacker ? AV_CODEC_ID_SMACKVIDEO
					: AV_CODEC_ID_VP9) &&
			*video < 0)
			*video = (int)i;
		else if (*audio < 0 &&
			is_audio_codec(parameters->codec_id, smacker))
			*audio = (int)i;
		else
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported movie tracks");
	}

	if (*video < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"movie has no video track");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_video(struct mh_ffmpeg_video *decoder, struct mh_ffmpeg_input *input,
	int track, struct mh_error *error_out)
{
	AVStream *stream;
	AVCodecParameters *parameters;
	const AVCodec *codec;
	int result;

	stream = input->format->streams[track];
	parameters = stream->codecpar;
	decoder->rate = av_guess_frame_rate(input->format, stream, NULL);

	if (parameters->width <= 0 || parameters->width > MH_WEBM_WIDTH_LIMIT ||
		parameters->height <= 0 ||
		parameters->height > MH_WEBM_HEIGHT_LIMIT ||
		decoder->rate.num <= 0 || decoder->rate.den <= 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported movie dimensions or frame rate");

	decoder->info.width = (unsigned int)parameters->width;
	decoder->info.height = (unsigned int)parameters->height;
	decoder->info.interval_us =
		1000000.0 * decoder->rate.den / decoder->rate.num;
	decoder->time_base = stream->time_base;

	if (decoder->info.interval_us < 1000 ||
		decoder->info.interval_us > 1000000 ||
		decoder->time_base.num <= 0 || decoder->time_base.den <= 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported movie timing");

	codec = avcodec_find_decoder(parameters->codec_id);

	if (codec == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"FFmpeg was built without the requested video decoder");

	decoder->codec = avcodec_alloc_context3(codec);
	decoder->frame = av_frame_alloc();
	decoder->rgba =
		malloc((size_t)decoder->info.width * decoder->info.height * 4);

	if (decoder->codec == NULL || decoder->frame == NULL ||
		decoder->rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie video decoder");

	result = avcodec_parameters_to_context(decoder->codec, parameters);
	decoder->codec->pkt_timebase = stream->time_base;
	decoder->codec->thread_count = 1;
	decoder->codec->max_pixels = MH_WEBM_WIDTH_LIMIT * MH_WEBM_HEIGHT_LIMIT;
	decoder->codec->err_recognition = AV_EF_EXPLODE | AV_EF_CRCCHECK;

	if (result >= 0)
		result = avcodec_open2(decoder->codec, codec, NULL);

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot initialize movie decoder");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
remember_frame(struct mh_ffmpeg_video *decoder, AVPacket *packet,
	size_t audio_end, struct mh_error *error_out)
{
	struct mh_video_packet *next;
	size_t capacity;
	double timestamp, expected, tolerance;

	if (packet->pts == mh_ffmpeg_no_timestamp || packet->size <= 0 ||
		decoder->count == MH_WEBM_FRAME_LIMIT ||
		(size_t)packet->size >
			MH_RESOURCE_READ_LIMIT - decoder->packet_bytes)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"movie frame index exceeds supported limits");

	timestamp = packet->pts * av_q2d(decoder->time_base);
	expected = decoder->count * av_q2d(av_inv_q(decoder->rate));
	tolerance = av_q2d(decoder->time_base) + 0.000001;

	if (fabs(timestamp - expected) > tolerance ||
		(decoder->count != 0 &&
			packet->pts <= decoder->packets[decoder->count - 1]
					.packet->pts))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"movie clip must start at zero and have a fixed frame cadence");

	if (decoder->count == decoder->capacity) {
		capacity = decoder->capacity == 0 ? 256 : decoder->capacity * 2;

		if (capacity > MH_WEBM_FRAME_LIMIT)
			capacity = MH_WEBM_FRAME_LIMIT;

		next = realloc(decoder->packets, capacity * sizeof(*next));

		if (next == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot grow movie frame index");

		decoder->packets = next;
		decoder->capacity = capacity;
	}

	next = decoder->packets + decoder->count;
	next->packet = av_packet_clone(packet);
	next->audio_end = audio_end;

	if (next->packet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot retain movie frame packet");

	decoder->count++;
	decoder->packet_bytes += (size_t)packet->size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
collect_packets(struct mh_ffmpeg_video *decoder, struct mh_ffmpeg_input *input,
	int video_track, int audio_track, struct mh_ffmpeg_audio *audio,
	struct mh_error *error_out)
{
	AVPacket *packet;
	enum mh_error_code status;
	int result;

	packet = av_packet_alloc();

	if (packet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie packet");

	status = MH_ERROR_NONE;

	while ((result = av_read_frame(input->format, packet)) >= 0) {
		if (packet->stream_index == video_track)
			status = remember_frame(decoder, packet,
				mh_ffmpeg_audio_frames(audio), error_out);
		else if (packet->stream_index == audio_track && audio != NULL)
			status = mh_ffmpeg_audio_packet(
				audio, packet, error_out);

		av_packet_unref(packet);

		if (status != MH_ERROR_NONE)
			break;
	}

	av_packet_free(&packet);

	if (status != MH_ERROR_NONE)
		return status;

	if (result != AVERROR_EOF || decoder->count == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"cannot read complete movie clip");

	decoder->info.frames = (unsigned long)decoder->count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ffmpeg_video_prepare(struct mh_ffmpeg_video *decoder,
	struct mh_ffmpeg_input *input, int sound, struct mh_error *error_out)
{
	struct mh_ffmpeg_audio *audio;
	struct mh_audio_decode_info audio_info;
	AVCodecParameters *parameters;
	enum mh_error_code status;
	int video_track, audio_track;

	audio = NULL;
	status = select_tracks(
		input, decoder->smacker, &video_track, &audio_track, error_out);

	if (status == MH_ERROR_NONE)
		status = open_video(decoder, input, video_track, error_out);

	if (status == MH_ERROR_NONE && audio_track >= 0) {
		parameters = input->format->streams[audio_track]->codecpar;

		if (parameters->sample_rate < 8000 ||
			parameters->sample_rate > 96000 ||
			(parameters->ch_layout.nb_channels != 1 &&
				parameters->ch_layout.nb_channels != 2))
			status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported movie audio format");
		else if (!decoder->smacker &&
			(parameters->sample_rate != 48000 ||
				parameters->ch_layout.nb_channels != 2))
			status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"expected stereo Opus movie audio");
		else {
			decoder->info.audio_rate =
				(unsigned long)parameters->sample_rate;
			decoder->info.audio_channels =
				(unsigned int)parameters->ch_layout.nb_channels;
			decoder->info.audio_bits = 16;

			if (sound)
				status = mh_ffmpeg_audio_open(
					input->format->streams[audio_track],
					MH_AUDIO_SAMPLE_BYTES_MAX, &audio,
					error_out);
		}
	}

	if (status == MH_ERROR_NONE)
		status = collect_packets(decoder, input, video_track,
			audio_track, audio, error_out);

	if (status == MH_ERROR_NONE && audio != NULL)
		status = mh_ffmpeg_audio_finish(
			audio, &decoder->audio, &audio_info, error_out);

	mh_ffmpeg_audio_close(audio);

	return status;
}
