#ifndef MH_RACE_SOUND_INTERNAL_H
#define MH_RACE_SOUND_INTERNAL_H

#include "engine/audio/race_sound.h"
#include "engine/core/error.h"

/* Apply race-bank priority/flags, then stop, adjust or request one layer. */
enum mh_error_code	mh_race_sound_update_layer(struct mh_mixer *mixer,
	const struct mh_sound_bank *bank, unsigned int voice,
	unsigned int sample, double pitch, double left, double right, int loop,
	struct mh_error *error_out);

/*
 * Mutable vehicle sound stages, called within a caller-owned transaction.
 * Public entry points validate field/bank limits before invoking them.
 */
enum mh_error_code	mh_race_sound_prepare(struct mh_race_sound *sound,
	const struct mh_field *field, struct mh_error *error_out);
enum mh_error_code	mh_race_sound_car(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	const struct mh_sound_listener *listener, double pedal,
	unsigned int entry, struct mh_error *error_out);
enum mh_error_code	mh_race_sound_gear(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	unsigned int entry, struct mh_error *error_out);

#endif
