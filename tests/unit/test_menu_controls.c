#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_controls.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Controls line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
unavailable_preferences(void)
{
	struct mh_menu_controls_state state;
	struct mh_menu_controls_input input;
	struct mh_menu_controls_output output;
	struct mh_control_profile profile, saved_profile;
	unsigned int i;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&profile, 0, sizeof(profile));
	state.preferences = 1;
	state.column = MH_MENU_CONTROLS_BINDINGS;
	state.unavailable = (1U << MH_MENU_CONTROLS_FEEDBACK) |
		(1U << MH_MENU_CONTROLS_MOUSE_Z);
	profile.layout.force_feedback = 1;

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++)
		profile.layout.mouse_speed[i] = 1;

	saved_profile = profile;
	input.left = 1;
	CHECK(mh_menu_controls_update(&state, &profile, 1, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&profile, &saved_profile, sizeof(profile)) == 0);
	CHECK(output.count == 1 &&
		output.events[0].value == MH_MENU_SOUND_VALUE_LIMIT);
	state.row = MH_MENU_CONTROLS_MOUSE_Z;
	input.left = 0;
	input.held_right = 1;
	input.elapsed_ms = 100;
	CHECK(mh_menu_controls_update(&state, &profile, 1, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&profile, &saved_profile, sizeof(profile)) == 0 &&
		output.count == 0);
	state.row = MH_MENU_CONTROLS_MOUSE_X;
	input.up = 1;
	CHECK(mh_menu_controls_update(&state, &profile, 1, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(profile.layout.mouse_speed[0] > 1 && profile.dirty == 1);
	CHECK(profile.layout.mouse_speed[1] == 1 &&
		profile.layout.mouse_speed[2] == 1);
	CHECK(profile.layout.force_feedback == 1 &&
		state.row == MH_MENU_CONTROLS_FEEDBACK);

	return 0;
}

static int
invalid(void)
{
	struct mh_menu_controls_state state, saved_state;
	struct mh_menu_controls_input input;
	struct mh_menu_controls_output output, saved_output;
	struct mh_control_profile profiles[3], saved_profiles[3];
	unsigned int i, count;

	for (i = 0; i < 15; i++) {
		memset(&state, 0, sizeof(state));
		memset(profiles, 0, sizeof(profiles));
		memset(&input, 0, sizeof(input));
		memset(&output, 0xa5, sizeof(output));
		state.phase = MH_MENU_CONTROLS_CAPTURE;
		input.held_count = 1;
		input.first_held = 42;
		count = 3;

		if (i == 0)
			state.column = 2;

		if (i == 1)
			state.row = 10;

		if (i == 2)
			state.selected = count;

		if (i == 3)
			state.phase = 4;

		if (i == 4)
			count = 0;

		if (i == 5)
			count = MH_MENU_CONTROL_CAPACITY + 1;

		if (i == 6)
			input.held_count = 257;

		if (i == 7)
			input.first_held = 256;

		if (i == 12)
			state.selected = UINT_MAX;

#if ULONG_MAX > MH_U32_MAX
		if (i == 13)
			profiles[2].layout.bindings[27] = (MH_U32_MAX + 1UL);

		if (i == 14)
			profiles[2].dirty = (MH_U32_MAX + 1UL);

#else
		if (i >= 13)
			continue;

#endif
		saved_state = state;
		saved_output = output;
		memcpy(saved_profiles, profiles, sizeof(saved_profiles));
		CHECK(mh_menu_controls_update(i == 8 ? NULL : &state,
			i == 9 ? NULL : profiles, count,
			i == 10 ? NULL : &input, i == 11 ? NULL : &output,
			NULL, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
			memcmp(&output, &saved_output, sizeof(output)) == 0);
		CHECK(memcmp(saved_profiles, profiles,
			sizeof(saved_profiles)) == 0);
	}

	return 0;
}

int
main(void)
{
	struct mh_menu_controls_state state;
	struct mh_menu_controls_input input;
	struct mh_menu_controls_output output;
	struct mh_control_profile profiles[3], saved_profiles[3];
	unsigned int i;

	CHECK(invalid() == 0 && unavailable_preferences() == 0);
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(profiles, 0, sizeof(profiles));

	for (i = 0; i < MH_CONTROL_BINDINGS; i++)
		profiles[1].layout.bindings[i] = i;

	strcpy(profiles[1].layout.name, "Custom");
	profiles[1].layout.mouse_speed[0] = 0.25f;
	profiles[1].layout.mouse_speed[1] = -1;
	profiles[1].layout.bindings[0] = MH_CONTROL_UNBOUND;
	profiles[1].layout.bindings[1] = profiles[1].layout.bindings[9] = 42;
	state.selected = 1;
	state.column = 1;
	memcpy(saved_profiles, profiles, sizeof(saved_profiles));
	input.enter = 1;
	input.held_count = 1;
	input.first_held = 28;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_RELEASE && output.count == 1);
	CHECK(output.events[0].kind == MH_MENU_SOUND &&
		output.events[0].value == 14);
	input.enter = 0;
	input.escape = 1;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE &&
		output.count == 1);
	CHECK(state.phase == MH_MENU_CONTROLS_RELEASE &&
		memcmp(saved_profiles, profiles, sizeof(saved_profiles)) == 0);
	input.held_count = 0;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_CAPTURE && output.count == 1);
	input.held_count = 2;
	input.first_held = 42;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_FINISH && output.count == 3);
	CHECK(output.events[0].value == 30 && output.events[1].value == 30 &&
		output.events[2].value == 29);
	saved_profiles[1].layout.bindings[0] = 42;
	saved_profiles[1].layout.bindings[1] =
		saved_profiles[1].layout.bindings[9] = MH_CONTROL_UNBOUND;
	saved_profiles[1].dirty = 1;
	CHECK(memcmp(saved_profiles, profiles, sizeof(saved_profiles)) == 0);
	input.first_held = 43;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE &&
		output.count == 0 && state.phase == MH_MENU_CONTROLS_RESTORE);
	CHECK(memcmp(saved_profiles, profiles, sizeof(saved_profiles)) == 0);
	input.held_count = 0;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_IDLE && output.count == 1);
	state.phase = MH_MENU_CONTROLS_CAPTURE;
	input.held_count = 1;
	input.first_held = 27;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_FINISH && output.count == 1 &&
		output.events[0].value == 31);
	CHECK(memcmp(saved_profiles, profiles, sizeof(saved_profiles)) == 0);

	/*
	 * The release timeout is strict and also wins over same-poll release.
	 */
	state.phase = MH_MENU_CONTROLS_RELEASE;
	state.capture_started = 1000;
	input.clock_ms = 9000;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_RELEASE && output.count == 1 &&
		output.events[0].kind == MH_MENU_CONTROLS_INPUT_MODE &&
		output.events[0].value == 0);
	input.clock_ms++;
	input.held_count = 0;
	CHECK(mh_menu_controls_update(&state, profiles, 3, &input, &output,
		NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MENU_CONTROLS_IDLE && output.count == 2 &&
		output.events[0].value == 0 &&
		output.events[1].kind == MH_MENU_CONTROLS_INPUT_MODE &&
		output.events[1].value == 1);
	CHECK(memcmp(saved_profiles, profiles, sizeof(saved_profiles)) == 0);
	puts("Controls: capture/release, all duplicate swaps, reserved keys, metadata preservation and atomic bounds passed");

	return 0;
}
