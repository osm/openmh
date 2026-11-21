#include "engine/audio/ds3d_listener.h"

void
mh_ds3d_listener_step(struct mh_spatial_listener_state *state,
	const struct mh_spatial_listener_input *input,
	struct mh_spatial_listener_update *output)
{
	mh_spatial_listener_step(state, input, output);
}
