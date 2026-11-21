#include <stddef.h>
#include <string.h>

#include "engine/audio/eax_source.h"

static void
position_relative(const struct mh_spatial_source_input *input,
	const struct mh_eax_source_listener *listener,
	struct mh_spatial_source_update *output)
{
	unsigned int axis;

	if (listener == NULL || !input->has_buffer)
		return;

	output->changed = 1;

	for (axis = 0; axis < 3; axis++) {
		output->velocity[axis] =
			(float)(((long double)listener->position[axis] -
					listener->previous[axis]) /
				input->elapsed);
		output->position[axis] = (float)((long double)input->pan *
				listener->right[axis] +
			listener->position[axis]);
	}
}

int
mh_eax_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input,
	const struct mh_eax_source_listener *listener, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output)
{
	memset(output, 0, sizeof(*output));

	if (input->spatial)
		mh_spatial_source_move(state, input, output);
	else
		position_relative(input, listener, output);

	parameters->frequency =
		(float)((long double)input->sample_rate * input->pitch);

	if (input->spatial) {
		if (!mh_spatial_source_in_range(input->position,
			listener->position, input->near_distance,
			input->far_distance))
			return 0;
	} else {
		parameters->gain = input->gain;
		parameters->pan = input->pan;

		if (parameters->gain <= 0)
			return 0;
	}

	mh_spatial_source_score(
		state, input->priority, master_gain, &parameters->gain);

	return 1;
}

unsigned int
mh_eax_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_EAX_VOICES])
{
	return mh_spatial_source_slot(score, voices, MH_EAX_VOICES);
}
