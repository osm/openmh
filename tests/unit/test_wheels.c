#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/wheels.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_wheel_input input;
	struct mh_wheel_state state, saved;
	double rates[2];

	memset(&input, 0, sizeof(input));
	memset(&state, 0, sizeof(state));
	input.ratio = 100;
	input.rpm = 5000;
	input.forward_velocity = 4;
	input.gear = input.up_ready = input.down_ready = input.rear_supported =
		1;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == 8 && rates[1] == 8);
	input.wheelspin = 1;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == 8 && rates[1] == 30);
	input.heavy_brake = 1;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == 0 && rates[1] == 0);
	input.heavy_brake = input.wheelspin = input.rear_supported = 0;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == 8 && rates[1] == 30);
	input.gear = 0;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == 8 && rates[1] == -30);
	input.down_ready = 0;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[1] == 8);
	input.down_ready = 1;
	input.up_ready = 0;
	input.forward_velocity = -4;
	CHECK(mh_wheel_rates(&input, rates, NULL) == MH_ERROR_NONE &&
		rates[0] == -8 && rates[1] == -8);
	input.ratio = HUGE_VAL;
	CHECK(mh_wheel_rates(&input, rates, NULL) != MH_ERROR_NONE &&
		rates[0] == -8 && rates[1] == -8);
	CHECK(mh_wheel_rates(NULL, rates, NULL) != MH_ERROR_NONE &&
		mh_wheel_rates(&input, NULL, NULL) != MH_ERROR_NONE);

	rates[0] = 8;
	rates[1] = -12;
	CHECK(mh_wheel_advance(&state, rates, .125, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.angle[0] == 1 && state.angle[1] == -1.5 &&
		state.variant[0] == 3 && state.variant[1] == 3);
	rates[0] = 1;
	rates[1] = 2;
	CHECK(mh_wheel_advance(&state, rates, .25, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.variant[0] == 0 && state.variant[1] == 1);
	rates[0] = 3;
	CHECK(mh_wheel_advance(&state, rates, .25, &state, NULL) ==
			MH_ERROR_NONE &&
		state.variant[0] == 2);
	state.angle[0] = 125.5;
	state.angle[1] = -125.5;
	rates[0] = 4;
	rates[1] = -4;
	CHECK(mh_wheel_advance(&state, rates, .25, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.angle[0] == 126.5 - 125.6636962890625 &&
		state.angle[1] == -state.angle[0]);
	state.angle[0] = 500;
	rates[0] = 0;
	CHECK(mh_wheel_advance(&state, rates, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.angle[0] > 125.6637);
	saved = state;
	rates[1] = HUGE_VAL;
	CHECK(mh_wheel_advance(&state, rates, .1, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	CHECK(mh_wheel_advance(&state, rates, -.1, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_wheel_advance(NULL, rates, 0, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_wheel_advance(&state, NULL, 0, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_wheel_advance(&state, rates, 0, NULL, NULL) != MH_ERROR_NONE);
	memset(&state, 0, sizeof(state));
	state.variant[0] = 3;
	state.variant[1] = 1;
	rates[0] = 0;
	rates[1] = 120;
	CHECK(mh_wheel_frame(&state, rates, 1.0f / 60, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.drawn[0] == 3 && state.drawn[1] == 1 &&
		state.variant[0] == 0 && state.variant[1] == 3 &&
		state.angle[0] == 0 && state.angle[1] == 2);
	state.angle[0] = 500;
	CHECK(mh_wheel_frame(&state, rates, 0, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.angle[0] == 500 && state.drawn[0] == 0 &&
		state.drawn[1] == 3 && state.variant[1] == 3);
	CHECK(mh_wheel_frame(&state, rates, 1, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.angle[0] == 500 - 125.6636962890625 &&
		state.angle[1] == 122);
	saved = state;
	CHECK(mh_wheel_frame(&state, rates, -1, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_wheel_frame(&state, rates, 1.01f, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_wheel_frame(&state, rates, 0, NULL, NULL) != MH_ERROR_NONE);

	return failures != 0;
}
