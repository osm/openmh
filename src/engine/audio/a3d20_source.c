#include <stddef.h>
#include <string.h>

#include "engine/audio/a3d20_source.h"

#define SOURCE_HEIGHT .5

static void
velocity_component(struct mh_a3d20_source_update *output, unsigned int axis,
	float position, float previous, float elapsed)
{
	long double delta;

	delta = (long double)position - previous;

	if (elapsed != 0) {
		output->velocity[axis] = (float)(delta / elapsed);
		return;
	}

	if (delta == 0)
		output->velocity_kind[axis] = MH_A3D20_VELOCITY_UNDEFINED;
	else if (delta > 0)
		output->velocity_kind[axis] =
			MH_A3D20_VELOCITY_POSITIVE_INFINITY;
	else
		output->velocity_kind[axis] =
			MH_A3D20_VELOCITY_NEGATIVE_INFINITY;
}

static void
reverse_velocity_z(struct mh_a3d20_source_update *output)
{
	if (output->velocity_kind[2] == MH_A3D20_VELOCITY_POSITIVE_INFINITY)
		output->velocity_kind[2] = MH_A3D20_VELOCITY_NEGATIVE_INFINITY;
	else if (output->velocity_kind[2] ==
		MH_A3D20_VELOCITY_NEGATIVE_INFINITY)
		output->velocity_kind[2] = MH_A3D20_VELOCITY_POSITIVE_INFINITY;
	else if (output->velocity_kind[2] == MH_A3D20_VELOCITY_FINITE)
		output->velocity[2] = -output->velocity[2];
}

static void
position_spatial(struct mh_spatial_source_state *state,
	const struct mh_a3d20_source_input *input,
	struct mh_a3d20_source_update *output)
{
	unsigned int axis;

	if (input->position == NULL)
		return;

	for (axis = 0; axis < 3; axis++) {
		if (input->has_source) {
			velocity_component(output, axis, input->position[axis],
				state->position[axis], input->elapsed);
			output->position[axis] = input->position[axis];
		}

		state->position[axis] = input->position[axis];
	}

	output->changed = input->has_source;

	if (output->changed)
		output->position[1] = (float)((long double)input->position[1] +
			SOURCE_HEIGHT);
}

static void
position_relative(const struct mh_a3d20_source_input *input,
	const struct mh_a3d20_source_listener *listener,
	struct mh_a3d20_source_update *output)
{
	unsigned int axis;

	if (listener == NULL)
		return;

	output->changed = 1;

	for (axis = 0; axis < 3; axis++) {
		velocity_component(output, axis, listener->position[axis],
			listener->previous[axis], input->elapsed);
		output->position[axis] = (float)((long double)input->pan *
				listener->right[axis] +
			listener->position[axis]);
	}
}

int
mh_a3d20_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_a3d20_source_input *input,
	const struct mh_a3d20_source_listener *listener, float master_gain,
	struct mh_a3d20_source_parameters *parameters,
	struct mh_a3d20_source_update *output)
{
	memset(output, 0, sizeof(*output));

	if (input->spatial)
		position_spatial(state, input, output);
	else
		position_relative(input, listener, output);

	if (output->changed) {
		output->position[2] = -output->position[2];
		reverse_velocity_z(output);
	}

	parameters->pitch = input->pitch;

	if (input->spatial) {
		if (listener != NULL &&
			!mh_spatial_source_in_range(input->position,
				listener->position, input->near_distance,
				input->far_distance))
			return 0;
	} else {
		parameters->gain = input->gain;
		parameters->pan = input->pan;
	}

	mh_spatial_source_score(
		state, input->priority, master_gain, &parameters->gain);

	return 1;
}

unsigned int
mh_a3d20_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_A3D20_VOICES])
{
	return mh_spatial_source_slot(score, voices, MH_A3D20_VOICES);
}
