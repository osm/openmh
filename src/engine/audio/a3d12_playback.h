#ifndef MH_A3D12_PLAYBACK_H
#define MH_A3D12_PLAYBACK_H

#include "engine/audio/spatial_playback.h"

/*
 * A3D 1.2 playback. See spatial_playback.h for the shared request and clock
 * contract.
 */
void	mh_a3d12_playback_start(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_a3d12_playback_step(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_spatial_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_a3d12_playback_stop(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	struct mh_spatial_playback_request *output);

#endif
