#ifndef MH_VIDEO_FFMPEG_PROVIDER_H
#define MH_VIDEO_FFMPEG_PROVIDER_H

struct mh_video_decoder_provider;

/*
 * Smacker and fixed-cadence VP9/Opus WebM clips; NULL when FFmpeg is
 * unavailable.
 */
const struct mh_video_decoder_provider	*mh_video_ffmpeg_decoder(void);

#endif
