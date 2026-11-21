#ifndef MH_AUDIO_SURFACE_INTERNAL_H
#define MH_AUDIO_SURFACE_INTERNAL_H

#include "engine/audio/surface.h"
#include "engine/core/error.h"

/* Pure calculations shared by detailed and positional playback. */
enum mh_error_code	mh_surface_slip_parameters(
	const struct mh_vehicle_state *vehicle, int *active, double *gain,
	struct mh_error *error_out);
enum mh_error_code	mh_surface_body_gains(
	const struct mh_materials *materials, int contact,
	unsigned long material, double published_impulse,
	const struct mh_body_sparks *sparks,
	struct mh_body_sound_parameters *output, struct mh_error *error_out);

/*
 * Mutable surface sound stages, called within a caller-owned transaction.
 * Public entry points validate field/bank limits before invoking them.
 */
enum mh_error_code	mh_surface_sound_prepare(struct mh_surface_sound *sound,
	const struct mh_field *field, struct mh_error *error_out);
enum mh_error_code	mh_surface_sound_burst(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	unsigned int entry, struct mh_error *error_out);
enum mh_error_code	mh_surface_sound_car(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	const struct mh_sound_listener *listener, int listener_mode,
	unsigned int entry, struct mh_error *error_out);

#endif
