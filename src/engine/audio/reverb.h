#ifndef MH_AUDIO_REVERB_H
#define MH_AUDIO_REVERB_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_reverb;

struct mh_reverb_settings {
	double	volume;	/* Wet gain, 0..1; dry samples are preserved. */
	double	decay;	/* Native low-frequency decay time, .1..20 seconds. */
	double	damping; /* Native feedback low-pass coefficient, 0..1. */
};

/*
 * Portable stereo reverberation, not an emulation of a hardware driver.
 * Allocates delay storage once; configure/process allocate nothing. Rate uses
 * mixer.h's bounds. Failed creation preserves *output; destroy accepts NULL.
 * The owner serializes all operations with its PCM callback.
 */
enum mh_error_code	mh_reverb_create(unsigned long rate,
	const struct mh_reverb_settings *settings, struct mh_reverb **output,
	struct mh_error *error_out);
void			mh_reverb_destroy(struct mh_reverb *reverb);

/* Keep the tail; changed wet volume ramps over five milliseconds. */
enum mh_error_code	mh_reverb_configure(struct mh_reverb *reverb,
	const struct mh_reverb_settings *settings, struct mh_error *error_out);

/* Drop the tail and snap volume to its configured target. */
void	mh_reverb_clear(struct mh_reverb *reverb);

/*
 * Add wet effects to interleaved stereo. Send contains only the desired effects
 * bus (exclude music/UI); it may equal stereo exactly. Other overlap is
 * invalid. Inputs must be finite PCM. No final clipping: the output owner
 * controls headroom/saturation. Zero volume leaves dry bytes unchanged while
 * warming the delay network. Pause emits silence and freezes tails and ramps.
 * Frames <= MH_MIXER_FRAMES_MAX. Invalid arguments leave output/state
 * unchanged.
 */
enum mh_error_code	mh_reverb_process(struct mh_reverb *reverb,
	const float *send, float *stereo, size_t frames, int paused,
	struct mh_error *error_out);

#endif
