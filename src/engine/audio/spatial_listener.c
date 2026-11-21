#include <string.h>

#include "engine/audio/spatial_listener.h"

void
mh_spatial_listener_velocity(float previous[3], const float position[3],
	float elapsed, float velocity[3])
{
	unsigned int axis;
	int first;

	first = previous[0] == 0 && previous[1] == 0 && previous[2] == 0;

	for (axis = 0; axis < 3; axis++) {
		if (first)
			velocity[axis] = 0;
		else
			velocity[axis] = (float)(((long double)position[axis] -
							previous[axis]) /
				elapsed);

		previous[axis] = position[axis];
	}
}

void
mh_spatial_listener_step(struct mh_spatial_listener_state *state,
	const struct mh_spatial_listener_input *input,
	struct mh_spatial_listener_update *output)
{
	unsigned int axis;

	memset(output, 0, sizeof(*output));

	if (input->elapsed == 0)
		return;

	output->changed = 1;

	for (axis = 0; axis < 3; axis++) {
		output->position[axis] = input->position[axis];
		output->front[axis] = input->front[axis];
		output->up[axis] = input->up[axis];
	}

	mh_spatial_listener_velocity(state->position, input->position,
		input->elapsed, output->velocity);
}
