#ifndef MH_AUDIO_VEHICLE_SOUND_H
#define MH_AUDIO_VEHICLE_SOUND_H

#include "engine/audio/race_sound.h"
#include "engine/audio/surface.h"
#include "engine/core/error.h"

/*
 * Original active-slot order: horn, engine, burst, gear, tyres/body/crash;
 * positional cars request horn, enemy engine, skid. Both banks and their
 * shared pool commit together; all remain unchanged on error. A NULL asset
 * bank skips its state, even if that state pointer is non-NULL.
 * When both banks are present they must share one pool, or both be unbound.
 * HUD cues and ambient sounds belong to later caller phases.
 */
enum mh_error_code	mh_vehicle_sound_update(struct mh_race_sound *race,
	const struct mh_sound_bank *race_bank, struct mh_surface_sound *surface,
	const struct mh_surface_bank *surface_bank,
	const struct mh_field *field, double pedal, int paused,
	struct mh_error *error_out);

/*
 * Publish listener/car geometry after motion without new requests, parameter
 * changes or PCM. Both mixer snapshots commit together; NULL skips a bank.
 * An invalid later car leaves both mixers unchanged. The shared pool, queued
 * requests and sample cursors are never changed by geometry publication.
 */
enum mh_error_code	mh_vehicle_sound_positions(struct mh_race_sound *race,
	struct mh_surface_sound *surface, const struct mh_field *field,
	struct mh_error *error_out);

#endif
