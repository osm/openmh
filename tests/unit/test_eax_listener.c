#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_listener.h"
#include "engine/core/binary.h"

static int
check_sequence(void)
{
	struct mh_eax_listener_state state, saved;
	struct mh_eax_listener_input input;
	struct mh_eax_listener_update output;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	input.elapsed = .125f;
	input.reverb = 1;
	input.position[0] = 10;
	input.front[2] = input.up[1] = 1;
	mh_eax_listener_step(&state, &input, &output);

	if (!output.listener_changed || !output.reverb_changed ||
		output.reverb != .35f || output.velocity[0] != 0)
		return 1;

	input.position[0] = 12;
	mh_eax_listener_step(&state, &input, &output);

	if (!output.reverb_changed || output.velocity[0] != 16)
		return 1;

	input.reverb = state.reverb;
	mh_eax_listener_step(&state, &input, &output);

	if (output.reverb_changed || !output.listener_changed)
		return 1;

	saved = state;
	input.elapsed = 0;
	input.position[0] = 100;
	input.reverb = 0;
	mh_eax_listener_step(&state, &input, &output);

	if (output.reverb_changed || output.listener_changed ||
		memcmp(&saved, &state, sizeof(state)) != 0)
		return 1;

	puts("EAX listener history, velocity, reverb cache and zero-time gate passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}
