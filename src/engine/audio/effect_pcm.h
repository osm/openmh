#ifndef MH_AUDIO_EFFECT_PCM_H
#define MH_AUDIO_EFFECT_PCM_H

#include "engine/audio/sound_pool.h"
#include "engine/core/error.h"
#include "interfaces/audio/pcm.h"

/* Original output format and channel flags (distinct from record flags). */
#define MH_EFFECT_PCM_RATE 22050
#define MH_EFFECT_PCM_FRAME_BYTES 4
#define MH_EFFECT_PCM_ACTIVE 1U
#define MH_EFFECT_PCM_LOOP 2U
#define MH_EFFECT_PCM_GAIN_MAX .125

/* Native per-call bound; the original callback requires a nonzero block. */
#define MH_EFFECT_PCM_FRAMES_MAX 8192

/*
 * PCM mixing state. Positions count mono signed-16 samples; gains are the
 * device's already-scaled binary32 values, in [0,1/8].
 */
struct mh_effect_pcm_channel {
	const struct mh_audio_pcm	*wave;
	size_t				cursor, loop_start, loop_end;
	unsigned long			fraction, whole_step, fraction_step;
	float				left, right;

	/* Bit 0 active, bit 1 loop. Other bits survive. */
	unsigned int			flags;
};

/*
 * Emit interleaved little-endian stereo signed-16 PCM at the device's
 * 22050 Hz rate. Output holds at least frames*4 bytes, frames <=8192.
 * Nearest/even rounding and packed stereo addition match the original.
 * A channel completing inside this block retains its entry cursor/fraction.
 * Reject invalid active channels without modifying state or output.
 * Output, channel state and input sample storage must not overlap.
 * A zero-length request validates active channels without writing output.
 */
enum mh_error_code	mh_effect_pcm_render(
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	unsigned char *stereo, size_t frames, struct mh_error *error_out);

#endif
