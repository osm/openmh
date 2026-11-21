#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_rankings.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Rankings line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_rankings_state state, saved_state;
	struct mh_menu_rankings_input input;
	struct mh_menu_rankings_output output, saved_output;
	struct mh_highscore_query query;
	const char *track_names[8] = {"RedRock", "NolbyHills", "NeoCity",
		"Atlantika", "OkkunSpeedway", "Olympos", "RuhrStadt",
		"Goldbridge"};

	char filename[272];
	unsigned int i;

	mh_menu_rankings_defaults(&state);
	CHECK(state.focus == 0 && state.category == 3 &&
		state.difficulty == 1 && state.record_class == 1);
	CHECK(mh_menu_rankings_init(&state, 0, 5, 7, 2, 0, 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.category == 2 && state.track == 5 &&
		state.lap_choice == 3 && state.difficulty == 2 &&
		state.record_class == 0);
	CHECK(mh_menu_rankings_query(&state, track_names, 8, &query, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_filename(&query, filename, sizeof(filename), NULL) ==
			MH_ERROR_NONE &&
		strcmp(filename, "SRHN10Olympos.MHI") == 0);
	memset(&input, 0, sizeof(input));
	input.up = input.right = 1;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.category == 1 && state.focus == 1 && output.reload == 1 &&
		output.events.count == 2);
	CHECK(output.events.events[0].value == 7 &&
		output.events.events[1].value == 27);
	input.up = 0;
	input.down = 1;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.track == 6 && state.focus == 2 && output.reload == 1);
	input.right = 0;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.lap_choice == 4);
	CHECK(mh_menu_rankings_query(&state, track_names, 8, &query, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_filename(&query, filename, sizeof(filename), NULL) ==
			MH_ERROR_NONE &&
		strcmp(filename, "BRMN15RuhrStadt.MHI") == 0);
	input.down = 0;
	input.left = 1;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.focus == 1 && output.reload == 0);
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.focus == 0);
	input.left = 0;
	input.up = input.right = 1;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.category == 0 && state.focus == 1);
	input.up = 0;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.focus == 1);
	CHECK(output.events.count == 1 && output.events.events[0].value == 28 &&
		output.reload == 0);
	CHECK(mh_menu_rankings_query(&state, track_names, 8, &query, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_filename(&query, filename, sizeof(filename), NULL) ==
			MH_ERROR_NONE &&
		strcmp(filename, "BLMN00RuhrStadt.MHI") == 0);
	input.right = 0;
	input.escape = input.up = input.down = 1;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.track == 6 && output.reload == 1);
	CHECK(output.events.count == 4 && output.events.events[0].value == 1 &&
		output.events.events[1].kind == MH_MENU_PAGE &&
		output.events.events[1].value == MH_MENU_MAIN &&
		output.events.events[2].value == 7 &&
		output.events.events[3].value == 8);
	saved_state = state;
	input.escape = input.up = input.down = input.left = input.right = 2;
	CHECK(mh_menu_rankings_update(&state, 8, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	CHECK(output.events.count == 0 && output.reload == 0);

	/*
	 * League entry retains track/difficulty/focus/division, even when the
	 * unused current selections would be invalid for a non-league entry.
	 */
	state.division = 2;
	state.focus = 3;
	CHECK(mh_menu_rankings_init(&state, 1, 99, 0, 99, 1, 8, NULL) ==
			MH_ERROR_NONE &&
		state.track == 6 && state.difficulty == 2);
	CHECK(state.category == 2 && state.focus == 3 && state.division == 2 &&
		state.lap_choice == 0 && state.record_class == 1);
	CHECK(mh_menu_rankings_init(&state, 2, 0, MH_U32_MAX, 0, 0, 8, NULL) ==
			MH_ERROR_NONE &&
		state.category == 1 && state.lap_choice == 5);
	state.category = 3;
	CHECK(mh_menu_rankings_query(&state, NULL, 8, &query, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_filename(&query, filename, sizeof(filename), NULL) ==
			MH_ERROR_NONE &&
		strcmp(filename, "LE1.MHI") == 0);
	saved_state = state;
	memset(&saved_output, 0x5a, sizeof(saved_output));

	for (i = 0; i < 7; i++) {
		state = saved_state;

		if (i == 0)
			state.focus = 5;

		if (i == 1)
			state.category = 4;

		if (i == 2)
			state.track = 8;

		if (i == 3)
			state.division = 3;

		if (i == 4)
			state.lap_choice = 6;

		if (i == 5)
			state.difficulty = 3;

		if (i == 6)
			state.record_class = 2;

		output = saved_output;
		CHECK(mh_menu_rankings_update(&state, 8, &input, &output,
			NULL) != MH_ERROR_NONE &&
			memcmp(&output, &saved_output, sizeof(output)) == 0);
	}

	state = saved_state;
	CHECK(mh_menu_rankings_init(&state, 7, 0, 5, 0, 0, 8, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	CHECK(mh_menu_rankings_update(&state, 0, &input, &output, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	CHECK(mh_menu_rankings_update(&state, 4097, &input, &output, NULL) !=
		MH_ERROR_NONE);
	state.category = 2;
	track_names[state.track] = "../outside";
	memset(&query, 0x5a, sizeof(query));
	CHECK(mh_menu_rankings_query(&state, track_names, 8, &query, NULL) !=
			MH_ERROR_NONE &&
		query.kind == 0x5a5a5a5aU);
	puts("Rankings: navigation/query sequence, retained league selections, lap ceiling, exact events and bounds passed");

	return 0;
}
