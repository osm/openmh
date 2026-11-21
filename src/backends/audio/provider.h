#ifndef MH_AUDIO_PROVIDER_H
#define MH_AUDIO_PROVIDER_H

struct mh_horn_source;
const struct mh_horn_source	*mh_horn_fallback_source(void);

struct mh_audio_decoder_provider;
const struct mh_audio_decoder_provider	*mh_audio_fallback_decoder(void);

#endif
