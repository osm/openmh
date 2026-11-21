#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_multi.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Multi line %d: %s\n", __LINE__, #x);  \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_multi_state state, saved_state;
	struct mh_menu_multi_input input;
	struct mh_menu_multi_output output, saved_output;
	struct mh_settings settings, saved_settings;
	unsigned int advance[256], i;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	CHECK(mh_settings_init(&settings, "Player", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	settings.protocol = 1;

	for (i = 0; i < 256; i++)
		advance[i] = 8;

	input.address_count = 3;
	input.protected_count = 2;
	state.address = 2;
	state.create_row = 2;
	CHECK(mh_menu_multi_enter(&state, NULL) == MH_ERROR_NONE &&
		state.create_row == 2 && state.address == 2);
	input.keys[MH_KEY_DOWN] = input.keys[MH_KEY_ENTER] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == 1 && state.choice == 1 && output.count == 2);
	memset(input.keys, 0, 256);
	state.create_row = 1;
	strcpy(settings.session_name, "Session");
	input.keys[30] = input.keys[MH_KEY_ENTER] = input.keys[MH_KEY_DOWN] = 1;
	input.characters[30] = 'X';
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(settings.session_name, "SessionX") == 0 &&
		state.create_row == 2 && output.cursor_slot == 7);
	CHECK(output.events[0].value == 11 &&
		output.events[1].kind == MH_MULTI_HOST &&
		output.events[2].value == 34);

	/* Create stops at Password; there is no selectable protocol row. */
	memset(input.keys, 0, sizeof(input.keys));
	memset(input.characters, 0, sizeof(input.characters));
	state.create_row = MH_MULTI_CREATE_PASSWORD;
	saved_settings = settings;
	input.keys[MH_KEY_DOWN] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.create_row == MH_MULTI_CREATE_PASSWORD &&
		output.count == 1 &&
		output.events[0].value == MH_MENU_SOUND_ROW_LIMIT);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_LEFT] = input.keys[MH_KEY_RIGHT] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 0);
	CHECK(memcmp(&saved_settings, &settings, sizeof(settings)) == 0);

	/* New addresses have a neutral name and two editable rows. */
	memset(input.keys, 0, sizeof(input.keys));
	memset(input.characters, 0, sizeof(input.characters));
	state.phase = MH_MULTI_EDIT;
	state.choice = MH_MULTI_CHOICE_JOIN;
	input.keys[MH_KEY_INSERT] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MULTI_ADD_ADDRESS &&
		strcmp(state.address_name, "Server 02") == 0);
	state.address_row = MH_MULTI_ADDRESS_NAME;
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_DOWN] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.address_row == MH_MULTI_ADDRESS_IP);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[30] = 1;
	input.characters[30] = 'x';
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(state.ip_address, "x") == 0 &&
		output.cursor_slot == MH_MULTI_IP_ADDRESS_SLOT);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_DOWN] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.address_row == MH_MULTI_ADDRESS_IP &&
		output.events[0].value == MH_MENU_SOUND_ROW_LIMIT);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_UP] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.address_row == MH_MULTI_ADDRESS_NAME);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_ENTER] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 2);
	CHECK(state.phase == MH_MULTI_EDIT &&
		output.events[1].kind == MH_MULTI_ADD);

	/* Pending deletion is deferred by the search notice and completion. */
	memset(input.keys, 0, 256);
	state.phase = 1;
	state.choice = 0;
	settings.menu_confirmation = 0;
	input.keys[MH_KEY_DELETE] = input.keys[MH_KEY_ENTER] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == 2 && state.delete_answer == 1 &&
		output.count == 1);
	memset(input.keys, 0, 256);
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		output.searching == 1);
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		output.events[0].kind == MH_MULTI_FIND);
	CHECK(state.phase == MH_MULTI_SEARCHING);
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 0 && state.delete_answer == 1);
	CHECK(mh_menu_multi_search_complete(&state, 0, NULL) == MH_ERROR_NONE &&
		state.phase == 1);
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.address == 1 && !state.delete_answer &&
		output.events[0].kind == MH_MULTI_DELETE);

	/*
	 * Invalid late deletion cannot leak an earlier page request or text
	 * edit.
	 */
	state.choice = 1;
	state.create_row = 1;
	state.delete_answer = 1;
	state.address = 0;
	input.keys[30] = input.keys[MH_KEY_ENTER] = 1;
	saved_state = state;
	saved_settings = settings;
	saved_output = output;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved_state, &state, sizeof(state)) == 0 &&
		memcmp(&saved_settings, &settings, sizeof(saved_settings)) ==
			0 &&
		memcmp(&saved_output, &output, sizeof(output)) == 0);
	state.delete_answer = 0;
	input.address_count = 0;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) != MH_ERROR_NONE);
	input.address_count = 3;
	advance[255] = 65536;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) != MH_ERROR_NONE);
	advance[255] = 8;
	memset(state.ip_address, 'X', 128);
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) != MH_ERROR_NONE);
	state.phase = MH_MULTI_SEARCHING;
	state.session = 4;
	CHECK(mh_menu_multi_search_complete(&state, 2, NULL) == MH_ERROR_NONE &&
		state.session == 1 && state.phase == 4);
	CHECK(mh_menu_multi_search_complete(&state, 2, NULL) != MH_ERROR_NONE);

	/* Legacy catalogs can still protect their built-in entries. */
	memset(&state, 0, sizeof(state));
	memset(input.keys, 0, 256);
	input.address_count = input.protected_count = 1;
	state.phase = 1;
	input.keys[MH_KEY_DELETE] = 1;
	settings.menu_confirmation = 0;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		output.count == 0);
	input.address_count = 2;
	state.address = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 && output.events[0].kind == MH_MULTI_DELETE &&
		state.address == 0);
	input.protected_count = 0;
	input.address_count = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 && output.events[0].kind == MH_MULTI_DELETE &&
		output.events[0].value == 0 && state.address == 0);

	/* Removing the last saved server leaves a usable empty address book. */
	input.address_count = 0;
	input.keys[MH_KEY_ENTER] = input.keys[MH_KEY_RIGHT] = 1;
	input.keys[MH_KEY_DOWN] = input.keys[MH_KEY_END] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.address == 0 && state.phase == MH_MULTI_EDIT);

	for (i = 0; i < output.count; i++)
		CHECK(output.events[i].kind == MH_MULTI_SOUND);

	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_INSERT] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MULTI_ADD_ADDRESS &&
		strcmp(state.address_name, "Server 01") == 0);
	memset(input.keys, 0, sizeof(input.keys));
	input.keys[MH_KEY_ENTER] = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MULTI_EDIT &&
		output.events[1].kind == MH_MULTI_ADD);
	input.address_count = 1;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(state.phase == MH_MULTI_SEARCH_NOTICE);

	/* An empty catalog cannot carry a pending search or deletion. */
	input.address_count = 0;
	CHECK(mh_menu_multi_update(&state, &settings, &input, advance, &output,
		NULL) != MH_ERROR_NONE);

	return 0;
}
