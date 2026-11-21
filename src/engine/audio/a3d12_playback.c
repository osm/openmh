#include "engine/audio/a3d12_playback.h"

void
mh_a3d12_playback_start(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output)
{
	mh_spatial_playback_start(state, input, clock, output);
}

void
mh_a3d12_playback_step(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_spatial_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output)
{
	mh_spatial_playback_step(state, input, status, clock, output);
}

void
mh_a3d12_playback_stop(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	struct mh_spatial_playback_request *output)
{
	mh_spatial_playback_stop(state, input, output);
}
