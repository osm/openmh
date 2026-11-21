#include <math.h>
#include <stddef.h>
#include <string.h>

#include "engine/audio/spatial_source.h"

#define DISTANCE_SCALE .8
#define SCORE_GAIN_SCALE .5

static float
source_distance(const float position[3], const float listener[3])
{
	float delta[3];
	long double squared;
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		delta[axis] =
			(float)((long double)position[axis] - listener[axis]);

	squared = (long double)delta[1] * delta[1] +
		(long double)delta[0] * delta[0] +
		(long double)delta[2] * delta[2];

	return (float)((long double)sqrt((double)squared) * DISTANCE_SCALE);
}

int
mh_spatial_source_in_range(
	const float position[3], const float anchor[3], float near, float far)
{
	float distance, span, ramp;

	distance = source_distance(position, anchor);
	span = (float)((long double)far - near);

	if (span <= 0 || distance > far)
		return 0;

	if (distance < near)
		return 1;

	ramp = (float)(1 - ((long double)distance - near) / span);

	return ramp > 0;
}

void
mh_spatial_source_move(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input,
	struct mh_spatial_source_update *output)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		if (input->has_buffer) {
			output->position[i] = input->position[i];
			output->velocity[i] =
				(float)(((long double)input->position[i] -
						state->position[i]) /
					input->elapsed);
		}

		state->position[i] = input->position[i];
	}

	output->changed = input->has_buffer;
}

void
mh_spatial_source_score(struct mh_spatial_source_state *state,
	unsigned long priority, float master_gain, float *gain)
{
	if (*gain > 1)
		*gain = 1;

	if (*gain < 0)
		*gain = 0;

	state->score = (float)((long double)priority +
		(long double)*gain * SCORE_GAIN_SCALE);
	*gain = (float)((long double)*gain * master_gain);
}

unsigned int
mh_spatial_source_slot(float score,
	const struct mh_spatial_source_state *const voices[],
	unsigned int count)
{
	unsigned int slot, lowest_index;

	lowest_index = 0;

	for (slot = 0; slot < count; slot++) {
		if (voices[slot] == NULL)
			return slot + 1;

		if (voices[slot]->score < voices[lowest_index]->score)
			lowest_index = slot;
	}

	return score > voices[lowest_index]->score ? lowest_index + 1 : 0;
}

int
mh_spatial_source_prepare_bound(struct mh_spatial_source_state *state,
	const struct mh_spatial_source_input *input, const float anchor[3],
	int secondary_bound, float master_gain,
	struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_source_update *output)
{
	memset(output, 0, sizeof(*output));

	if (input->spatial) {
		if (anchor == NULL || !secondary_bound)
			return 0;

		mh_spatial_source_move(state, input, output);
	}

	parameters->frequency =
		(float)((long double)input->sample_rate * input->pitch);

	if (input->spatial) {
		if (!mh_spatial_source_in_range(input->position, anchor,
			input->near_distance, input->far_distance))
			return 0;
	} else {
		parameters->gain = input->gain;
		parameters->pan = input->pan;
	}

	mh_spatial_source_score(
		state, input->priority, master_gain, &parameters->gain);

	return 1;
}
