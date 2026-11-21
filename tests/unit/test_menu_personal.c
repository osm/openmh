#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_personal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Personal line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_personal_state state, saved_state;
	struct mh_menu_personal_input input;
	struct mh_menu_personal_output output, saved_output;
	struct mh_settings settings, saved_settings;
	unsigned long colors[32];
	unsigned int advance[256], i;

	CHECK(mh_settings_init(&settings, "Player", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	memset(&state, 0, sizeof(state));
	state.selected = 2;

	for (i = 0; i < 256; i++)
		advance[i] = 12;

	for (i = 0; i < 32; i++)
		colors[i] = i * 0x080808UL;

	settings.player_color = 0;
	input.keys[MH_KEY_LEFT] = 1;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(settings.player_color == colors[31] && output.swatch_count == 1 &&
		output.swatches[0].color == 0);
	CHECK(output.swatches[0].active == 1 &&
		output.swatches[0].nearest == 0 &&
		output.events[0].value == 12);
	input.keys[MH_KEY_RIGHT] = 1;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(settings.player_color == 0 && output.swatches[0].nearest == 31 &&
		output.count == 2);
	state.selected = 13;
	input.keys[MH_KEY_LEFT] = 0;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(settings.custom_car_colors == 1 && output.swatch_count == 4);
	memset(&input, 0, sizeof(input));
	state.selected = 1;
	strcpy(settings.team_name, "SWE");
	input.keys[30] = input.keys[48] = input.keys[MH_KEY_ENTER] =
		input.keys[MH_KEY_ESCAPE] = 1;
	input.characters[30] = 'X';
	input.characters[48] = 'Y';
	input.clock_ms = 99;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(settings.team_name, "SWEX") == 0 && output.cursor &&
		output.cursor_row == 1 && output.cursor_width == 48);
	CHECK(output.count == 6 && output.events[0].value == 34 &&
		output.events[1].value == 35);
	CHECK(output.events[2].value == 11 &&
		output.events[3].kind == MH_MENU_PAGE &&
		output.events[4].value == 1);
	saved_settings = settings;
	saved_output = output;
	saved_state = state;
	colors[31] = 0x1000000UL;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved_settings, sizeof(settings)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0 &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	colors[31] = 0;
	advance[255] = 65536;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) != MH_ERROR_NONE);
	advance[255] = 12;
	memset(settings.short_keys[9], 'X', 128);
	saved_settings = settings;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&settings, &saved_settings, sizeof(settings)) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	settings.short_keys[9][127] = 0;
	state.selected = 17;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) != MH_ERROR_NONE);
	state.selected = 16;
	settings.custom_car_colors = 0;
	memset(&input, 0, sizeof(input));
	input.keys[MH_KEY_DOWN] = input.keys[MH_KEY_RIGHT] = 1;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 16 && output.swatch_count == 1 &&
		output.count == 2 && output.events[0].value == 9 &&
		output.events[1].value == 13);

	/* Maximal byte-editor callbacks still fit the complete page output. */
	state.selected = 12;
	memset(settings.short_keys[9], 'A', 127);
	settings.short_keys[9][127] = 0;
	memset(input.keys, 1, 256);
	memset(input.characters, 'B', 256);

	for (i = 0; i < 256; i++)
		advance[i] = MH_U16_MAX;

	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(output.count <= 784 && output.cursor_width == 0 &&
		settings.short_keys[9][0] == 0);

	CHECK(mh_settings_init(&settings, "Player", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	state.patched = 1;
	state.selected = MH_MENU_PERSONAL_SYSTEM;
	memset(&input, 0, sizeof(input));
	input.keys[MH_KEY_DOWN] = 1;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 4 && output.cursor && output.cursor_row == 4);
	input.clock_ms = 500;
	input.keys[MH_KEY_DOWN] = 0;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(!output.cursor && output.cursor_row == 4);
	input.keys[MH_KEY_DOWN] = 1;
	state.selected = 14;
	input.keys[MH_KEY_RIGHT] = 1;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(settings.custom_car_colors && state.selected == 14);
	input.keys[MH_KEY_RIGHT] = 0;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 15 && output.swatches[1].active);
	state.selected = 17;
	CHECK(mh_menu_personal_update(&state, &settings, colors, &input,
		advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == 17 && output.events[0].value == 9);

	return 0;
}
