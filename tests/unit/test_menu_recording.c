#include <stdio.h>
#include <string.h>

#include "engine/assets/font.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_recording_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "save menu line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_save_flow(void)
{
	struct mh_menu_recording state, before;
	struct mh_menu_recording_input input;
	struct mh_menu_recording_output output, held;
	unsigned int advance[MH_FONT_GLYPHS], i;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		advance[i] = 12;

	strcpy((char *)state.name, "Remember");
	CHECK(mh_menu_recording_begin(&state, NULL) == MH_ERROR_NONE &&
		state.phase == 2 &&
		strcmp((char *)state.name, "Remember") == 0);
	CHECK(mh_menu_recording_update(
		&state, &input, advance, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.phase == 3 && output.count == 1 &&
		output.events[0].kind == MH_MENU_DIALOG &&
		output.events[0].value == MH_MENU_CONFIRM_RECORD_SAVE);
	state.answer = 1;
	input.keys[MH_KEY_ENTER] = 1;
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.pending_probe);
	before = state;
	held = output;
	CHECK(mh_menu_recording_update(
		&state, &input, advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_recording_finish_probe(&state, 2, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0 &&
		memcmp(&output, &held, sizeof(output)) == 0);
	CHECK(mh_menu_recording_finish_probe(&state, 1, &output, NULL) ==
			MH_ERROR_NONE &&
		state.phase == 7 && state.answer == 0);
	state.answer = 2;
	input.keys[MH_KEY_ENTER] = 0;
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.phase == 5 && !output.draw_name);
	input.keys[MH_KEY_ESCAPE] = 1;
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.phase == 2);
	CHECK(strcmp((char *)state.name, "Remember") == 0);
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.phase == 3);
	state.answer = 2;
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) == MH_ERROR_NONE &&
		state.phase == 0 && output.count == 1 &&
		output.events[0].kind == MH_MENU_RECORDING_RETURN);
	before = state;
	memset(state.name, 'X', sizeof(state.name));
	held = output;
	CHECK(mh_menu_recording_update(&state, &input, advance, &output,
		NULL) != MH_ERROR_NONE &&
		memcmp(&output, &held, sizeof(output)) == 0);
	state = before;
	state.phase = 5;
	input.keys[MH_KEY_ESCAPE] = 0;
	advance[0] = 65536;
	before = state;
	CHECK(mh_menu_recording_update(
		&state, &input, advance, &output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0 &&
		memcmp(&output, &held, sizeof(output)) == 0);

	return 0;
}

int
main(void)
{
	return check_save_flow();
}
