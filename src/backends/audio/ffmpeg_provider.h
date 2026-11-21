#ifndef MH_AUDIO_FFMPEG_PROVIDER_H
#define MH_AUDIO_FFMPEG_PROVIDER_H

struct mh_audio_decoder_provider;

/* Ogg Opus; reports UNSUPPORTED when FFmpeg is unavailable in this build. */
const struct mh_audio_decoder_provider	*mh_audio_ffmpeg_decoder(void);

#endif
