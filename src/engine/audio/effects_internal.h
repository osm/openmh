#ifndef MH_AUDIO_EFFECTS_INTERNAL_H
#define MH_AUDIO_EFFECTS_INTERNAL_H

#include "engine/audio/effects.h"

/*
 * Shared precondition for scheduler, buffer and diagnostic render operations.
 */
int	mh_effects_banks_valid(const struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count);

/*
 * Publish only the record's authoritative physical-channel cursor. Older
 * aliases retain their own PCM but must not overwrite that logical cursor.
 * Call after bank validation; no ownership or activity changes occur here.
 */
void	mh_effects_publish_cursors(const struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_mixer *const *banks);

#endif
