#include <string.h>

#include "engine/audio/eax_listener.h"
#include "engine/audio/spatial_listener.h"

#define REVERB_SCALE .35

void
mh_eax_listener_step(struct mh_eax_listener_state *state,
	const struct mh_eax_listener_input *input,
	struct mh_eax_listener_update *output)
{
	unsigned int axis;

	memset(output, 0, sizeof(*output));

	if (input->elapsed == 0)
		return;

	if (input->reverb != state->reverb) {
		output->reverb_changed = 1;
		output->reverb =
			(float)((long double)input->reverb * REVERB_SCALE);
		state->reverb = output->reverb;
	}

	output->listener_changed = 1;

	for (axis = 0; axis < 3; axis++) {
		output->position[axis] = input->position[axis];
		output->front[axis] = input->front[axis];
		output->up[axis] = input->up[axis];
	}

	mh_spatial_listener_velocity(state->position, input->position,
		input->elapsed, output->velocity);
}
