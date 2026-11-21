#include "engine/audio/mixer_internal.h"
#include "engine/audio/race_effects.h"

unsigned int
mh_race_effect_active_voices(const struct mh_mixer *mixer)
{
	unsigned int voice, count;

	count = 0;

	for (voice = 0; voice < MH_MIXER_VOICES; voice++)
		count += mixer->voices[voice].active != 0;

	return count;
}

enum mh_error_code
mh_race_effect_clear_pool(struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int count,
	struct mh_error *error_out)
{
	unsigned int stopped, i;
	enum mh_error_code status;

	status = mh_sound_pool_clear(pool, &stopped, error_out);

	for (i = 0; i < count; i++)
		mh_mixer_sync(banks[i]);

	return status;
}
