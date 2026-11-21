#ifndef MH_PAUSE_SOUND_H
#define MH_PAUSE_SOUND_H

#include "engine/audio/mixer.h"
#include "engine/core/error.h"

enum mh_pause_sound_sample {
	MH_PAUSE_SOUND_LOOP,
	MH_PAUSE_SOUND_ENTER,
	MH_PAUSE_SOUND_CURSOR,
	MH_PAUSE_SOUND_ERROR,
	MH_PAUSE_SOUND_SAMPLES
};

/* Original records in order: menyloop, enter, cursor, error. Borrowed PCM. */
struct mh_pause_sound_bank {
	struct mh_audio_pcm	waves[MH_PAUSE_SOUND_SAMPLES];

	/* One bit per optional cue missing from the content. */
	unsigned int		absent_samples;
};

enum mh_error_code	mh_pause_sound_init(unsigned long rate, double gain,
	struct mh_mixer *mixer, struct mh_error *error_out);

/*
 * Original request gate: active menu sounds are not restarted. The background
 * sample loops; all four samples have priority 3 and unit pitch/ear gains.
 */
enum mh_error_code	mh_pause_sound_play(struct mh_mixer *mixer,
	const struct mh_pause_sound_bank *bank, unsigned int index,
	struct mh_error *error_out);

#endif
