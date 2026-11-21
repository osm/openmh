#ifndef MH_DS3D_PLAYBACK_H
#define MH_DS3D_PLAYBACK_H

#include "engine/audio/spatial_playback.h"

/* DS3D profile entry points; see the shared request and clock contract. */
void	mh_ds3d_playback_start(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_ds3d_playback_step(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	const struct mh_spatial_playback_status *status,
	const struct mh_clock_source *clock,
	struct mh_spatial_playback_request *output);
void	mh_ds3d_playback_stop(struct mh_spatial_playback_state *state,
	const struct mh_spatial_playback_input *input,
	struct mh_spatial_playback_request *output);

#endif
