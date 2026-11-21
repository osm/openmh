#ifndef MH_AUDIO_SOUND_VARIANT_H
#define MH_AUDIO_SOUND_VARIANT_H

#define MH_SOUND_VARIANT_INITIAL 13UL

/*
 * Engine bursts, slide releases and crashes share one counter to alternate
 * samples.
 */
unsigned int	mh_sound_variant_next(unsigned long *counter);

#endif
