#include <stddef.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/pause_sound.h"
#include "engine/core/error.h"

#define PAUSE_SOUND_PRIORITY 3
#define LOOP_FLAGS 1

enum mh_error_code
mh_pause_sound_init(unsigned long rate, double gain, struct mh_mixer *mixer,
	struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	status = mh_mixer_init(rate, gain, mixer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_PAUSE_SOUND_SAMPLES; i++)
		mixer->priority[i] = PAUSE_SOUND_PRIORITY;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_pause_sound_play(struct mh_mixer *mixer,
	const struct mh_pause_sound_bank *bank, unsigned int index,
	struct mh_error *error_out)
{
	struct mh_mixer next;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;

	if (mixer == NULL || bank == NULL || index >= MH_PAUSE_SOUND_SAMPLES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid menu sound");

	if ((bank->absent_samples & (1U << index)) != 0 ||
		mixer->voices[index].active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	mh_mixer_begin(mixer, &next, &pool_copy);
	status = mh_mixer_policy(&next, index,
		index == MH_PAUSE_SOUND_LOOP ? LOOP_FLAGS : 0,
		PAUSE_SOUND_PRIORITY, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_play(&next, index, &bank->waves[index], 1, 1,
			1, 0,
			index == MH_PAUSE_SOUND_LOOP ? bank->waves[index].frames
						: 0,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_mixer_commit(&next, mixer);
	*mixer = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
