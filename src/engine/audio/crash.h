#ifndef MH_AUDIO_CRASH_H
#define MH_AUDIO_CRASH_H

#include "engine/audio/mixer.h"
#include "engine/core/error.h"
#include "engine/simulation/impact.h"

#define MH_CRASH_CLASSES 16
#define MH_CRASH_CAR_SAMPLES 4
#define MH_CRASH_LIGHT_SAMPLES 2
#define MH_CRASH_SAMPLES (MH_CRASH_CAR_SAMPLES + MH_CRASH_CLASSES)

/*
 * 0/1 light cars (barrel/crash2, pitch .8); 2/3 heavy cars (majcrash,
 * pitch 1). 4+class holds an ESD ObjectColSound; class zero uses cars.
 */
struct mh_crash_sample {
	struct mh_audio_pcm	wave;
	int			enabled;

	/*
	 * Registered object cue whose optional PCM is absent from the content.
	 */
	int			absent;
	unsigned int		priority;
	double			volume, pitch;
};

struct mh_crash_bank {
	struct mh_crash_sample	samples[MH_CRASH_SAMPLES];
};

struct mh_crash_sound {
	int		bound; /* -1 until the first definition is copied. */
	unsigned int	last_class;

	/* Original stored binary32, before device saturation. */
	double		gain;

	/* Active original record whose sample/channel was unloaded. */
	int		detached;
};

void	mh_crash_sound_init(struct mh_crash_sound *sound);

/*
 * Capture next-load state before releasing the old voice. Retain the class,
 * gain and unreachable activity gate, but no sample binding. A later global
 * channel stop cannot find this record; a new sample copy replaces it.
 */
enum mh_error_code	mh_crash_sound_unload(struct mh_crash_sound *sound,
	int active, struct mh_error *error_out);

/*
 * Stateful crash sound consumer. available bits 1..15 are registered ESD
 * classes. active is the output voice's current state (in addition to any
 * detached activity retained by sound). random is shared with engine bursts and
 * slide releases. Outputs unchanged on invalid input.
 */
enum mh_error_code	mh_crash_select(struct mh_crash_sound *sound,
	const struct mh_impact_effects *effects, unsigned int available,
	int active, unsigned long *random, int *play,
	struct mh_error *error_out);

/*
 * One logical voice: changing to a known class replaces the previous sound.
 * Missing classes preserve the cached record, gain and pitch. Bank and PCM
 * must outlive playback. Explicitly absent PCM stops playback without losing
 * its class binding. Zero ESD pitch means effective 22050 Hz.
 */
enum mh_error_code	mh_crash_update(struct mh_crash_sound *sound,
	struct mh_mixer *mixer, unsigned int voice,
	const struct mh_crash_bank *bank,
	const struct mh_impact_effects *effects, unsigned long *random,
	struct mh_error *error_out);

#endif
