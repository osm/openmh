#ifndef MH_EAX_SOURCE_H
#define MH_EAX_SOURCE_H

#define MH_EAX_VOICES 24

#include "engine/audio/spatial_source.h"

struct mh_eax_source_listener {
	float	position[3], previous[3], right[3];
};

/*
 * Return eligibility, retaining position even when out of range. Inputs are
 * finite; elapsed must be positive when velocity is requested. A spatial source
 * requires a listener; nonspatial sources allow NULL. Nonspatial sources reject
 * nonpositive gain after updating geometry/frequency.
 *
 * Parameters are deliberately in/out: spatial preparation leaves pan and the
 * incoming gain alone until the common gain clamp/master-volume step. It does
 * not multiply gain by the distance ramp. Pass initialized parameters;
 * eax_queue.h makes that seed and subsequent shared values explicit.
 *
 * Both requests are deferred: spatial sources send position then velocity;
 * listener-relative sources send velocity then position.
 */
int	mh_eax_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input,
	const struct mh_eax_source_listener *listener, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output);

/* Returns a one-based slot, or zero if no voice can be replaced. */
unsigned int	mh_eax_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_EAX_VOICES]);

#endif
