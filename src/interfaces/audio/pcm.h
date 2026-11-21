#ifndef MH_INTERFACES_AUDIO_PCM_H
#define MH_INTERFACES_AUDIO_PCM_H

#include <stddef.h>

/*
 * Borrowed interleaved PCM, independent of its encoded source. One or two
 * channels, unsigned 8-bit or signed little-endian 16-bit samples. Frames
 * count complete channel groups; bytes is frames * channels * (bits / 8).
 * The operation producing this view specifies the backing storage lifetime.
 */
struct mh_audio_pcm {
	const unsigned char	*pcm;
	size_t			bytes, frames;
	unsigned long		rate;
	unsigned int		channels, bits;
};

#endif
