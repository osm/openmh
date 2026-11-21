#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_source.h"
#include "engine/core/binary.h"

static int
sequence(void)
{
	struct mh_spatial_source_state state;
	struct mh_a3d20_source_input input;
	struct mh_a3d20_source_listener listener;
	struct mh_a3d20_source_parameters parameters;
	struct mh_a3d20_source_update output;
	float position[3];

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&listener, 0, sizeof(listener));
	memset(&parameters, 0, sizeof(parameters));
	input.spatial = input.has_source = 1;
	input.elapsed = .125f;
	input.pitch = 2;
	input.near_distance = 5;
	input.far_distance = 40;
	input.position = position;
	position[0] = 25;
	position[1] = 2;
	position[2] = 3;
	parameters.gain = .5f;

	if (!mh_a3d20_source_prepare(
		&state, &input, &listener, .5f, &parameters, &output) ||
		parameters.gain != .25f || parameters.pitch != 2 ||
		!output.changed || output.position[1] != 2.5f ||
		output.position[2] != -3 || output.velocity[2] != -24 ||
		state.position[2] != 3)
		return 1;

	position[0] = 100;

	if (mh_a3d20_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		state.position[0] != 100 || !output.changed ||
		parameters.gain != .25f)
		return 1;

	/* Without a pose the original skips range rejection. */
	if (!mh_a3d20_source_prepare(
		&state, &input, NULL, 1, &parameters, &output))
		return 1;

	input.spatial = 0;
	input.gain = 0;
	input.pan = -.5f;
	listener.position[0] = 10;
	listener.right[0] = 1;

	if (!mh_a3d20_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		parameters.gain != 0 || output.position[0] != 9.5f ||
		output.position[1] != 0 || output.velocity[0] != 80 ||
		state.position[0] != 100)
		return 1;

	input.elapsed = 0;
	listener.position[1] = -1;
	listener.position[2] = 1;

	if (!mh_a3d20_source_prepare(
		&state, &input, &listener, 1, &parameters, &output) ||
		output.velocity_kind[0] !=
			MH_A3D20_VELOCITY_POSITIVE_INFINITY ||
		output.velocity_kind[1] !=
			MH_A3D20_VELOCITY_NEGATIVE_INFINITY ||
		output.velocity_kind[2] != MH_A3D20_VELOCITY_NEGATIVE_INFINITY)
		return 1;

	input.spatial = 1;
	input.position = state.position;

	if (!mh_a3d20_source_prepare(
		&state, &input, NULL, 1, &parameters, &output) ||
		output.velocity_kind[0] != MH_A3D20_VELOCITY_UNDEFINED ||
		output.velocity_kind[1] != MH_A3D20_VELOCITY_UNDEFINED ||
		output.velocity_kind[2] != MH_A3D20_VELOCITY_UNDEFINED)
		return 1;

	puts("A3D 2.0 source coordinates, pitch, retained motion and range gates passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
