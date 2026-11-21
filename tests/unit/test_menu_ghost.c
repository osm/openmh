#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_ghost.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Ghost menu line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_ghost_state state, saved_state;
	struct mh_menu_ghost_input input;
	struct mh_menu_ghost_output output, saved_output;
	struct mh_menu_dialog_text text;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	state.mode = 6;
	state.focus = 2;
	state.count = 3;
	state.selected = 2;
	state.delete_answer = 1;
	CHECK(mh_menu_ghost_enter(&state, NULL) == MH_ERROR_NONE);
	CHECK(state.focus == 0 && state.mode == 6 && state.selected == 2 &&
		state.delete_answer == 0);
	input.keys.right = input.keys.enter = input.keys.escape = 1;
	CHECK(mh_menu_ghost_update(&state, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.focus == 0 && output.count == 4 &&
		output.events[2].kind == MH_MENU_PAGE &&
		output.events[2].value == MH_MENU_QUICK &&
		output.events[3].value == 13);
	memset(&input, 0, sizeof(input));
	state.focus = 2;
	state.mode = 3;
	input.remove = input.keys.enter = input.keys.escape = 1;
	CHECK(mh_menu_ghost_update(&state, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.pending_delete && state.selected == 1 &&
		state.delete_answer == 0 && output.count == 3);
	CHECK(output.events[0].kind == MH_MENU_GHOST_DELETE &&
		output.events[0].value == 2);
	CHECK(output.events[1].kind == MH_MENU_GHOST_NAME &&
		output.events[1].value == 1);
	saved_state = state;
	saved_output = output;
	CHECK(mh_menu_ghost_update(&state, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_menu_ghost_finish_delete(
		&state, 2, 2, &input, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(mh_menu_ghost_finish_delete(
		&state, 2, 1, &input, &output, NULL) == MH_ERROR_NONE);
	CHECK(!state.pending_delete && state.focus == 0 && output.count == 4);
	CHECK(output.events[0].kind == MH_MENU_GHOST_NAME &&
		output.events[2].value == MH_MENU_CAR_SETUP);
	state.focus = 2;
	state.selected = 0;
	state.count = 1;
	CHECK(mh_menu_ghost_update(&state, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_menu_ghost_finish_delete(
		&state, 0, 0, &input, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.focus == 0 && output.count == 3 &&
		output.events[0].value == 15 &&
		output.events[2].value == MH_MENU_SINGLE);
	state.focus = 2;
	state.selected = 1;
	state.count = 2;
	state.delete_answer = 1;
	input.confirmation = 2;
	input.keys.enter = input.keys.escape = 0;
	CHECK(mh_menu_ghost_update(&state, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.delete_answer == 0 && !state.pending_delete &&
		output.count == 1);
	CHECK(output.events[0].kind == MH_MENU_DIALOG &&
		output.events[0].value == MH_MENU_CONFIRM_DEMO_DELETE);
	CHECK(mh_menu_dialog_builtin(MH_MENU_CONFIRM_DEMO_DELETE, &text,
		NULL) == MH_ERROR_NONE);
	CHECK(text.title_count == 3 && text.choice_count == 2);
	puts("Ghost focus, retained mode, deletion/refresh order, confirmation, simultaneous keys and atomic errors passed");

	return 0;
}
