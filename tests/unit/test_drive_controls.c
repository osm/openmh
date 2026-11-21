#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/drive_controls.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Driving controls line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_binding_input input;
	struct mh_control_binding_set bindings;
	struct mh_drive_controls state, saved;
	unsigned int i;

	memset(&input, 0, sizeof(input));
	memset(&bindings, 0, sizeof(bindings));
	memset(&state, 0, sizeof(state));

	for (i = 0; i < 10; i++)
		bindings.codes[i] = 0x200 + 30 + i;

	input.down[30] = 128;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.command == 8 && state.steering == -.5f && state.brake == 0);
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.steering == -.65f);
	input.down[31] = 128;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.command == 4 && state.steering == .5f);
	input.down[30] = input.down[31] = 0;
	input.down[33] = 128;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 0, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.command == 0 && state.brake == 0 && state.steering == 0);
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.command == 2 && state.brake == .7f);
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.brake == .825f);
	input.down[32] = input.down[34] = input.down[35] = input.down[37] = 128;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.command == 0x20 && state.throttle == 1 &&
		state.brake == .95f && state.handbrake == 1);
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 1, 0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.brake == 0 && state.throttle == 1 && state.handbrake == 1);
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 1,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(state.brake == 0 && state.throttle == 0 && state.handbrake == 0 &&
		state.command == 0);
	saved = state;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, -1, 1, 0, 0,
		&state, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &state, sizeof(state)) == 0);

	/*
	 * A late failed binding must not publish the earlier successful pedals.
	 */
	input.mouse[0] = MH_I32_MAX;
	input.mouse_speed[0] = -FLT_MAX;
	bindings.codes[MH_CONTROL_RIGHT] = 0x2100;
	CHECK(mh_drive_controls_step(&bindings, &input, &state, .25f, 1, 0, 0,
		&state, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &state, sizeof(state)) == 0);

	return 0;
}
