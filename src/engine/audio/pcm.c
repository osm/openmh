#include "engine/audio/pcm.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

double
mh_audio_pcm_sample(
	const struct mh_audio_pcm *pcm, size_t frame, unsigned int channel)
{
	size_t index;
	unsigned long value;
	long sample;

	if (frame >= pcm->frames || channel >= pcm->channels)
		return 0;

	index = (frame * pcm->channels + channel) * (pcm->bits / 8);

	if (pcm->bits == 8)
		return ((int)pcm->pcm[index] - 128) / 128.0;

	value = mh_read_u16le(pcm->pcm + index);
	sample = value >= MH_U16_SIGN_BIT ? (long)value - (long)MH_U16_MODULUS
					: (long)value;

	return sample / (double)MH_U16_SIGN_BIT;
}
