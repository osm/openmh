#include <stdio.h>
#include <string.h>

/* Both profiles submit the same deferred listener requests. */
#ifdef MH_TEST_A3D12
#include "engine/audio/a3d12_listener.h"
#define LISTENER_STATE mh_spatial_listener_state
#define LISTENER_INPUT mh_spatial_listener_input
#define LISTENER_UPDATE mh_spatial_listener_update
#define LISTENER_STEP mh_a3d12_listener_step
#else
#include "engine/audio/ds3d_listener.h"
#define LISTENER_STATE mh_spatial_listener_state
#define LISTENER_INPUT mh_spatial_listener_input
#define LISTENER_UPDATE mh_spatial_listener_update
#define LISTENER_STEP mh_ds3d_listener_step
#endif
#include "engine/core/binary.h"

static int
check_sequence(void)
{
	struct LISTENER_STATE state, saved;
	struct LISTENER_INPUT input;
	struct LISTENER_UPDATE output;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	input.elapsed = .125f;
	input.position[0] = 10;
	input.front[2] = input.up[1] = 1;
	LISTENER_STEP(&state, &input, &output);

	if (!output.changed || output.velocity[0] != 0)
		return 1;

	input.position[0] = 12;
	LISTENER_STEP(&state, &input, &output);

	if (!output.changed || output.velocity[0] != 16)
		return 1;

	saved = state;
	input.elapsed = 0;
	input.position[0] = 100;
	LISTENER_STEP(&state, &input, &output);

	if (output.changed || memcmp(&saved, &state, sizeof(state)) != 0)
		return 1;

	puts("Deferred listener history, velocity and zero-time gate passed");

	return 0;
}

int
main(void)
{
	return check_sequence();
}
