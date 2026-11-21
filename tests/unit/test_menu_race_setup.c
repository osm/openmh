#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_race_setup.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Race setup line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_race_setup_state state, saved_state;
	struct mh_menu_keys keys;
	struct mh_menu_output output, saved_output;
	unsigned long laps;

	memset(&state, 0, sizeof(state));
	memset(&keys, 0, sizeof(keys));
	state.track = 7;
	state.laps = 5;
	state.track_auxiliary = 99;
	keys.left = keys.right = keys.up = keys.down = keys.enter =
		keys.escape = 1;
	CHECK(mh_menu_race_setup_update(
		&state, 0, 8, 3, &keys, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.track == 2 && state.selected == 1 &&
		state.track_auxiliary == 0 && output.count == 8);
	CHECK(output.events[0].value == 12 && output.events[1].value == 13 &&
		output.events[2].value == 9);
	CHECK(output.events[3].value == 8 && output.events[4].value == 37 &&
		output.events[5].value == 37);
	CHECK(output.events[6].value == 1 &&
		output.events[7].kind == MH_MENU_PAGE &&
		output.events[7].value == MH_MENU_SINGLE);
	state.selected = 0;
	CHECK(mh_menu_race_setup_update(
		&state, 0, 8, 8, &keys, &output, NULL) == MH_ERROR_NONE &&
		output.count == 8);
	CHECK(output.events[5].kind == MH_MENU_PAGE &&
		output.events[5].value == MH_MENU_CAR_SETUP);
	CHECK(output.events[7].value == MH_MENU_SINGLE);
	memset(&keys, 0, sizeof(keys));
	keys.down = 1;
	keys.right = 1;
	state.selected = 2;
	state.catch_up = 0;
	CHECK(mh_menu_race_setup_update(
		&state, 2, 8, 8, &keys, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 2 && state.catch_up == 1 &&
		output.events[1].value == 9);
	saved_state = state;
	saved_output = output;
	CHECK(mh_menu_race_setup_update(
		&state, 0, 8, 0, &keys, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(mh_menu_race_setup_update(
		&state, 0, 8, 9, &keys, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(mh_menu_race_setup_update(NULL, 0, 8, 8, &keys, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_menu_race_laps(4, 0, &laps, NULL) == MH_ERROR_NONE &&
		laps == 3);
	CHECK(mh_menu_race_laps(4, 1, &laps, NULL) == MH_ERROR_NONE &&
		laps == 5);
	CHECK(mh_menu_race_laps(25, 0, &laps, NULL) == MH_ERROR_NONE &&
		laps == 15);
	CHECK(mh_menu_race_laps(0, 0, &laps, NULL) == MH_ERROR_NONE &&
		laps == 1);
	CHECK(mh_menu_race_laps(MH_U32_MAX, 1, &laps, NULL) == MH_ERROR_NONE &&
		laps == 25);
	CHECK(mh_menu_race_laps(5, 2, &laps, NULL) != MH_ERROR_NONE &&
		laps == 25);
	CHECK(mh_menu_race_laps(5, 1, NULL, NULL) != MH_ERROR_NONE);
	puts("Race setup locked-track rollback, request ordering, retained rows and lap boundaries passed");

	return 0;
}
