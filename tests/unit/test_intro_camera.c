#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/intro_camera.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_intro_camera_state state;
	struct mh_intro_camera_input input;
	struct mh_intro_camera_result result, saved;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	state.enabled = state.saved_in_car = state.in_car =
		state.requested_in_car = 1;
	input.has_first_motion = input.has_second_motion = 1;
	input.start_ms = 1;
	input.now_ms = 1;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.count == 1 && result.samples[0].frame == 0 &&
		result.state.chase && !result.state.in_car);
	input.now_ms = 6001;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 0);
	input.now_ms = 7001;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.count == 1 && result.samples[0].frame == 12.5f &&
		result.samples[0].motion_index == 0);
	input.now_ms = 9001;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.count == 1 && result.samples[0].frame == 12.5f &&
		result.samples[0].motion_index == 1);
	state = result.state;
	input.now_ms = 10001;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
			MH_ERROR_NONE &&
		result.state.enabled);
	input.now_ms = 10002;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(!result.state.enabled && result.state.in_car &&
		!result.state.chase && !result.count);
	input.now_ms = 12001;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
			MH_ERROR_NONE &&
		result.state.enabled);

	/* Zero release clock still executes subsequent branches in order. */
	input.start_ms = 0;
	input.now_ms = 9000;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.count == 2 && result.samples[0].motion_index == 0 &&
		result.samples[1].motion_index == 1);
	saved = result;
	input.now_ms = -1;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);
	input.now_ms = 0;
	state.enabled = 2;
	CHECK(mh_intro_camera_step(&state, &input, &result, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);

	return 0;
}
