#include "engine/audio/ds3d_source.h"

int
mh_ds3d_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input, const float anchor[3],
	int secondary_bound, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output)
{
	return mh_spatial_source_prepare_bound(state, input, anchor,
		secondary_bound, master_gain, parameters, output);
}

unsigned int
mh_ds3d_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_DS3D_VOICES])
{
	return mh_spatial_source_slot(score, voices, MH_DS3D_VOICES);
}
