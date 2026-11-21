#ifndef MH_DS3D_LISTENER_H
#define MH_DS3D_LISTENER_H

#include "engine/audio/spatial_listener.h"

/*
 * Finite inputs and nonnegative elapsed seconds. Zero elapsed requests nothing
 * and preserves history. Otherwise submit deferred orientation, position,
 * velocity, then commit in that order. An all-zero previous position suppresses
 * velocity. There is no EAX reverb request. History advances even if driver
 * calls fail. Zero-initialize once; clearing voices does not reset listener
 * history.
 */
void	mh_ds3d_listener_step(struct mh_spatial_listener_state *state,
	const struct mh_spatial_listener_input *input,
	struct mh_spatial_listener_update *output);

#endif
