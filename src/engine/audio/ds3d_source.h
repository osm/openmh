#ifndef MH_DS3D_SOURCE_H
#define MH_DS3D_SOURCE_H

#include "engine/audio/spatial_source.h"

#define MH_DS3D_VOICES 16

/*
 * Finite inputs; positive elapsed when moving a spatial source. Anchor is the
 * position in the first original binding; secondary_bound represents the second
 * binding's presence. Their caller ownership is separate. Missing either
 * rejects spatial sources without changing history/parameters. Nonspatial
 * sounds need neither binding and produce no position requests. Gain/pan are
 * in/out scratch: spatial sources retain both until gain scoring.
 * Zero/nonpositive nonspatial gain is clamped, not rejected. Spatial requests
 * are deferred position then velocity; rejected range still retains motion.
 */
int	mh_ds3d_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input, const float anchor[3],
	int secondary_bound, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output);

/* Return a one-based voice slot, or zero when admission is rejected. */
unsigned int	mh_ds3d_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_DS3D_VOICES]);

#endif
