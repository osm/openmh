#include "engine/audio/a3d12_listener.h"

void
mh_a3d12_listener_step(struct mh_spatial_listener_state *state,
	const struct mh_spatial_listener_input *input,
	struct mh_spatial_listener_update *output)
{
	mh_spatial_listener_step(state, input, output);
}
