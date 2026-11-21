#ifndef MH_ASSETS_WAVE_H
#define MH_ASSETS_WAVE_H

#include "engine/core/error.h"
#include "interfaces/audio/pcm.h"

/*
 * Bounded RIFF/WAVE PCM reader: mono/stereo unsigned 8 or signed 16 bits.
 * Unknown chunks and their padding are skipped. No allocation; output unchanged
 * on failure. PCM data must remain alive while the view is in use.
 */
enum mh_error_code	mh_audio_wave_decode(const unsigned char *data,
	size_t size, struct mh_audio_pcm *output, struct mh_error *error_out);

#endif
