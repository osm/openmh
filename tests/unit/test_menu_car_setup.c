#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_car_setup.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Car setup line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_car_setup_state state, saved_state;
	struct mh_menu_car_catalog catalog, saved_catalog;
	struct mh_menu_keys keys;
	struct mh_menu_car_output output, saved_output;
	unsigned long divisions[4];
	unsigned int i;

	memset(&state, 0, sizeof(state));
	memset(&catalog, 0, sizeof(catalog));
	memset(&keys, 0, sizeof(keys));

	for (i = 0; i < 4; i++)
		divisions[i] = 100UL * (i + 1) + 1;

	catalog.divisions = divisions;
	catalog.count = catalog.visible = 4;
	catalog.unlocked = 2;
	state.car = 3;
	state.automatic = 1;
	keys.left = keys.right = keys.up = keys.down = keys.enter =
		keys.escape = 1;
	CHECK(mh_menu_car_setup_update(&state, &catalog, 0, 3, 29, &keys,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 1 && state.car == 1 && state.automatic == 1 &&
		output.count == 9);
	CHECK(output.events[0].value == 9 && output.events[1].value == 8);
	CHECK(output.events[2].value == 20 && output.events[3].value == 19 &&
		output.events[4].value == 36);
	CHECK(output.events[5].value == 1 &&
		output.events[6].value == MH_MENU_RACE_SETUP);
	CHECK(output.events[7].value == 11 &&
		output.events[8].kind == MH_MENU_PAGE &&
		output.events[8].value == MH_MENU_QUICK);
	memset(&keys, 0, sizeof(keys));
	keys.enter = 1;
	state.selected = 2;
	state.car = 3;
	state.horn = 28;
	CHECK(mh_menu_car_setup_update(&state, &catalog, 1, 3, 29, &keys,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 && output.events[0].kind == MH_MENU_HORN &&
		output.events[0].value == 28);
	state.selected = 0;
	state.car = 1;
	CHECK(mh_menu_car_setup_update(&state, &catalog, 1, 3, 29, &keys,
		&output, NULL) == MH_ERROR_NONE &&
		output.count == 0);
	saved_state = state;
	saved_output = output;
	CHECK(mh_menu_car_setup_update(&state, &catalog, 1, 3, 0, &keys,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	divisions[0] = 110;
	divisions[1] = 210;
	divisions[2] = 310;
	divisions[3] = 101;
	state.car = 3;
	CHECK(mh_menu_car_enter(&state, &catalog, 1, 1, NULL) == MH_ERROR_NONE);
	CHECK(state.car == 0 && catalog.visible == 1 &&
		catalog.saved_visible == 4 && divisions[0] == 110);
	CHECK(mh_menu_car_leave(&catalog, 1, NULL) == MH_ERROR_NONE &&
		catalog.visible == 4);
	state.car = 3;
	CHECK(mh_menu_car_enter(&state, &catalog, 1, 0, NULL) ==
			MH_ERROR_NONE &&
		catalog.saved_visible == 1);
	CHECK(mh_menu_car_leave(&catalog, 1, NULL) == MH_ERROR_NONE &&
		catalog.visible == 1);
	catalog.visible = 4;
	state.car = 3;
	CHECK(mh_menu_car_enter(&state, &catalog, 0, 0, NULL) ==
			MH_ERROR_NONE &&
		catalog.saved_visible == 4 && catalog.visible == 1);
	CHECK(mh_menu_car_leave(&catalog, 0, NULL) == MH_ERROR_NONE &&
		catalog.visible == 1);
	divisions[3] = 410;
	saved_state = state;
	saved_catalog = catalog;
	CHECK(mh_menu_car_enter(&state, &catalog, 1, 1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);
	catalog.unlocked = 0;
	saved_catalog = catalog;
	CHECK(mh_menu_car_leave(&catalog, 1, NULL) != MH_ERROR_NONE &&
		memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);
	puts("Car setup ordering, nine-event launch, horn preview, League prefix and atomic errors passed");

	return 0;
}
