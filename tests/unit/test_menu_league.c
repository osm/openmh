#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_league.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "league menu line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_league_state state, saved_state;
	struct mh_menu_league_output output, saved_output;
	struct mh_menu_league_new_input input;
	struct mh_menu_keys keys;
	unsigned int advance[256], i;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&keys, 0, sizeof(keys));
	state.count = 1;
	strcpy((char *)state.name, "Draft");

	for (i = 0; i < 256; i++)
		advance[i] = 12;

	/*
	 * Escape with confirmation Off keeps the draft; On asks, then No drops
	 * it only after a catalog replacement succeeds.
	 */
	input.keys[MH_KEY_ESCAPE] = 1;
	CHECK(mh_menu_league_new_update(
		&state, &input, advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 2 && output.events[0].value == 11 &&
		output.events[1].value == MH_MENU_LEAGUE_SETUP);
	input.confirmation = 1;
	CHECK(mh_menu_league_new_update(
		&state, &input, advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 &&
		output.events[0].value == MH_MENU_CONFIRM_LEAGUE_CREATE);
	input.keys[MH_KEY_ESCAPE] = 0;
	state.create_answer = 2;
	CHECK(mh_menu_league_new_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.pending_delete == 2);
	saved_state = state;
	saved_output = output;
	CHECK(mh_menu_league_finish_remove(&state, 0, 0, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_menu_league_new_update(
		&state, &input, advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(mh_menu_league_finish_remove(&state, 1, 0, &output, NULL) ==
			MH_ERROR_NONE &&
		!state.pending_delete && !state.create_answer);
	CHECK(output.count == 2 && output.events[0].value == 1);

	/* Selection is live: Escape preserves Down's new league index. */
	state.count = 4;
	keys.down = keys.escape = 1;
	CHECK(mh_menu_league_load_update(&state, &keys, &output, NULL) ==
			MH_ERROR_NONE &&
		state.selected == 1 && state.list_row == 1);
	CHECK(output.count == 4 &&
		output.events[1].kind == MH_MENU_LEAGUE_SELECT &&
		output.events[1].value == 1);

	/* An answered deletion happens after Escape's page request. */
	state.selected = 3;
	state.delete_answer = 1;
	keys.down = 0;
	CHECK(mh_menu_league_delete_update(&state, &keys, 1, &output, NULL) ==
			MH_ERROR_NONE &&
		state.pending_delete == 1);
	CHECK(output.count == 5 &&
		output.events[4].kind == MH_MENU_LEAGUE_REMOVE);
	CHECK(mh_menu_league_finish_remove(&state, 3, 2, &output, NULL) ==
			MH_ERROR_NONE &&
		state.delete_row == 2 && state.delete_answer == 0);
	saved_state = state;
	saved_output = output;
	CHECK(mh_menu_league_finish_remove(&state, 3, 2, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_menu_league_delete_update(&state, &keys, 3, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	advance[0] = 65536;
	CHECK(mh_menu_league_new_update(
		&state, &input, advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	memset(state.name, 'X', sizeof(state.name));
	saved_state = state;
	CHECK(mh_menu_league_load_update(&state, &keys, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);

	return 0;
}
