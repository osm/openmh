#ifndef MH_BACKENDS_AUDIO_WAVE_PROVIDER_H
#define MH_BACKENDS_AUDIO_WAVE_PROVIDER_H

struct mh_audio_decoder_provider;

/* Stateless process-lifetime provider for original RIFF/WAVE PCM. */
const struct mh_audio_decoder_provider	*mh_audio_wave_decoder(void);

#endif
