#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_listener.h"
#include "engine/core/binary.h"

static int
sequence(void)
{
	struct mh_a3d20_listener_state state;
	struct mh_a3d20_listener_input input;
	struct mh_a3d20_listener_update output;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	input.elapsed = .125f;
	input.position[2] = 10;
	input.front[2] = 1;
	input.up[1] = 1;
	mh_a3d20_listener_step(&state, &input, &output);

	if (!output.changed || output.position[2] != -10 ||
		output.front[2] != -1 || output.up[1] != 1 ||
		output.velocity[2] != 0 || state.position[2] != 10)
		return 1;

	mh_a3d20_listener_step(&state, NULL, &output);

	if (output.changed || state.position[2] != 10)
		return 1;

	input.position[2] = 12;
	mh_a3d20_listener_step(&state, &input, &output);

	if (!output.changed || output.velocity[2] != -16 ||
		state.position[2] != 12)
		return 1;

	input.position[2] = 0;
	mh_a3d20_listener_step(&state, &input, &output);

	if (output.velocity[2] != 96)
		return 1;

	input.position[2] = 10;
	mh_a3d20_listener_step(&state, &input, &output);

	if (output.velocity[2] != 0)
		return 1;

	puts("A3D 2.0 listener coordinates, first-frame motion and NULL pose passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
