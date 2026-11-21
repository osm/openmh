#ifndef MH_VIDEO_FFMPEG_INTERNAL_H
#define MH_VIDEO_FFMPEG_INTERNAL_H

#include "backends/media/ffmpeg_input.h"
#include "backends/video/ffmpeg_limits.h"
#include "interfaces/video/decoder.h"

struct mh_video_packet {
	AVPacket	*packet;
	size_t		audio_end;
};

struct mh_ffmpeg_video {
	struct mh_video_decode_info	info;
	AVCodecContext			*codec;
	AVFrame				*frame;
	struct SwsContext		*conversion;
	struct mh_video_packet		*packets;
	size_t				count, capacity, position, packet_bytes;
	AVRational			rate, time_base;
	struct mh_file			audio;
	size_t				audio_position;
	unsigned char			*rgba;
	int				smacker;
};

enum mh_error_code	mh_ffmpeg_video_prepare(struct mh_ffmpeg_video *decoder,
	struct mh_ffmpeg_input *input, int audio, struct mh_error *error_out);
enum mh_error_code	mh_ffmpeg_video_read(struct mh_ffmpeg_video *decoder,
	struct mh_video_frame *output, struct mh_error *error_out);

#endif
