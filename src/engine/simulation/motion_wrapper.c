#include <math.h>
#include <string.h>

#include "engine/simulation/motion_internal.h"

#define DISPLAY_HEIGHT 0.3199999928474426

/* Both normalization stages and both cross products store floats. */
static double
axis_length(const double vector[3])
{
	volatile double value;

	value = vector[1] * vector[1];
	value += vector[0] * vector[0];
	value += vector[2] * vector[2];
	value = sqrt(value);

	return value;
}

void
mh_motion_normalize(double rotation[9])
{
	double x[3], y[3], z[3], size;
	volatile double value, inverse;
	unsigned int i, j, k;

	for (i = 0; i < 3; i++) {
		x[i] = (float)rotation[i];
		y[i] = (float)rotation[i + 3];
	}

	size = axis_length(x);

	if (size > 0) {
		inverse = 1 / size;

		for (i = 0; i < 3; i++) {
			value = x[i] * inverse;
			x[i] = (float)value;
		}
	} else {
		x[0] = 1;
		x[1] = x[2] = 0;
	}

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		value = x[j] * y[k];
		value -= x[k] * y[j];
		z[i] = (float)value;
	}

	size = axis_length(z);

	if (size <= 0) {
		memset(rotation, 0, 9 * sizeof(*rotation));
		rotation[0] = rotation[4] = rotation[8] = 1;
		return;
	}

	inverse = 1 / size;

	for (i = 0; i < 3; i++) {
		value = z[i] * inverse;
		z[i] = (float)value;
	}

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		value = z[j] * x[k];
		value -= z[k] * x[j];
		rotation[i + 3] = (float)value;
		rotation[i] = x[i];
		rotation[i + 6] = z[i];
	}
}

void
mh_motion_enter(struct mh_motion_state *state)
{
	unsigned int i;

	/* Finish the display-to-internal subtraction with its store. */
	for (i = 0; i < 3; i++)
		state->position[i] = (float)state->position[i];
}

void
mh_motion_leave(struct mh_motion_state *state)
{
	volatile double value;
	unsigned int i;

	/* Restore the display origin before normalization. */
	for (i = 0; i < 3; i++) {
		value = (double)(float)state->rotation[3 + i] * DISPLAY_HEIGHT;
		value += (double)(float)state->position[i];
		state->position[i] = (float)value;
	}

	mh_motion_normalize(state->rotation);

	/*
	 * The native API keeps an internal origin between slices. Retain the
	 * original next-entry subtraction, before its float store: rounding it
	 * here would lose the stored display pose when publishing to renderers.
	 */
	for (i = 0; i < 3; i++) {
		value = state->rotation[3 + i] * DISPLAY_HEIGHT;
		value = state->position[i] - value;
		state->position[i] = value;
	}
}
