#ifndef MH_AUDIO_PCM_H
#define MH_AUDIO_PCM_H

#include "interfaces/audio/pcm.h"

/*
 * Normalize one channel sample to [-1, 1). A valid PCM view is required;
 * an out-of-range frame or channel returns silence. No resampling or gain.
 */
double	mh_audio_pcm_sample(const struct mh_audio_pcm *pcm, size_t frame,
	unsigned int channel);

#endif
