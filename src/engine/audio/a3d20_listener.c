#include <string.h>

#include "engine/audio/a3d20_listener.h"
#include "engine/audio/spatial_listener.h"

void
mh_a3d20_listener_step(struct mh_a3d20_listener_state *state,
	const struct mh_a3d20_listener_input *input,
	struct mh_a3d20_listener_update *output)
{
	unsigned int axis;

	memset(output, 0, sizeof(*output));

	if (input == NULL)
		return;

	output->changed = 1;

	for (axis = 0; axis < 3; axis++) {
		output->position[axis] = input->position[axis];
		output->front[axis] = input->front[axis];
		output->up[axis] = input->up[axis];
	}

	mh_spatial_listener_velocity(state->position, input->position,
		input->elapsed, output->velocity);
	output->position[2] = -output->position[2];
	output->front[2] = -output->front[2];
	output->up[2] = -output->up[2];
	output->velocity[2] = -output->velocity[2];
}
