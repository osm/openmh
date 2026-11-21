#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/settings/av.h"
#include "engine/ui/hud.h"
#include "engine/ui/pause.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
test_invalid_inputs(void)
{
	struct mh_pause_state state, saved_state;
	struct mh_pause_settings settings, saved_settings;
	struct mh_pause_input input;
	struct mh_pause_output output, saved_output;
	unsigned int test;

	CHECK(mh_pause_init(NULL, NULL) != MH_ERROR_NONE);

	for (test = 0; test < 20; test++) {
		memset(&state, 0, sizeof(state));
		memset(&settings, 0, sizeof(settings));
		memset(&input, 0, sizeof(input));
		memset(&output, 0x5a, sizeof(output));
		settings.perspective = 16;
		settings.distance = 100;
		input.keys = MH_PAUSE_TOGGLE;

		switch (test) {
		case 0:
			state.page = 3;
			break;

		case 1:
			state.selected[0] = 5;
			break;

		case 2:
			state.selected[1] = 4;
			break;

		case 3:
			state.selected[2] = 3;
			break;

		case 4:
			state.visible = -1;
			break;

		case 5:
			state.visible = 2;
			break;

		case 6:
			settings.shading = 3;
			break;

		case 7:
			settings.perspective = 17;
			break;

		case 8:
			settings.distance = 29;
			break;

		case 9:
			settings.distance = 151;
			break;

		case 10:
			settings.effects_volume = 256;
			break;

		case 11:
			settings.cd_volume = 256;
			break;

		case 12:
			input.keys = 128;
			break;

		case 13:
			input.mode = 7;
			break;

		case 14:
			input.cd_playing = 2;
			break;

		case 15:
			input.restart_ok = -1;
			break;

		default:
			break;
		}

		saved_state = state;
		saved_settings = settings;
		saved_output = output;
		CHECK(mh_pause_update(test == 16 ? NULL : &state,
			test == 17 ? NULL : &settings,
			test == 18 ? NULL : &input,
			test == 19 ? NULL : &output,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0);
		CHECK(memcmp(&settings, &saved_settings, sizeof(settings)) ==
			0);
		CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	}
}

static void
test_draw(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list, saved_list;
	struct mh_pause_state state;
	struct mh_pause_settings display;
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	memset(&display, 0, sizeof(display));
	display.perspective = 16;
	display.distance = 100;
	mh_pause_init(&state, NULL);
	state.visible = 1;
	assets.atlas.image.width = assets.atlas.image.height = 256;
	assets.hardware_height = 12;

	for (i = 0; i < 256; i++) {
		assets.hardware_advance[i] = 8;
		assets.hardware_glyphs[i].width = 7;
		assets.hardware_glyphs[i].height = 12;
	}

	mh_ui_begin(&list, 640, 480);
	CHECK(mh_pause_draw(&assets, &state, &display, 0, 0, &list) &&
		list.count > 0);
	saved_list = list;
	state.visible = 0;
	CHECK(mh_pause_draw(&assets, &state, &display, 0, 0, &list) &&
		memcmp(&saved_list, &list, sizeof(list)) == 0);
	state.visible = 1;
	assets.hardware_glyphs['Q'].x = 257;
	CHECK(!mh_pause_draw(&assets, &state, &display, 0, 0, &list) &&
		list.count == saved_list.count);
	CHECK(memcmp(list.vertices, saved_list.vertices,
		list.count * sizeof(list.vertices[0])) == 0);
	assets.hardware_glyphs['Q'].x = 0;
	list.count = MH_UI_MAX_QUADS * 6;
	CHECK(!mh_pause_draw(&assets, &state, &display, 0, 0, &list) &&
		list.count == MH_UI_MAX_QUADS * 6);
	CHECK(!mh_pause_draw(&assets, &state, &display, 7, 0, &list));
}

int
main(void)
{
	struct mh_pause_state state, saved_state;
	struct mh_pause_settings settings, saved_settings;
	struct mh_pause_input input;
	struct mh_pause_output output, saved_output;

	test_invalid_inputs();
	test_draw();
	CHECK(mh_pause_init(&state, NULL) == MH_ERROR_NONE);
	memset(&settings, 0, sizeof(settings));
	memset(&input, 0, sizeof(input));
	settings.shading = 2;
	settings.perspective = 16;
	settings.distance = 100;
	settings.effects_volume = settings.cd_volume = 254;
	input.restart_ok = 1;
	input.keys = MH_PAUSE_TOGGLE;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.visible && state.page == 0);
	CHECK(output.dimmed == 1);
	CHECK(output.count == 4 &&
		output.events[0].kind == MH_PAUSE_DEVICE_STOP);
	CHECK(output.events[1].kind == MH_PAUSE_PLAY &&
		output.events[1].value == MH_PAUSE_LOOP);
	state.selected[0] = 3;
	input.keys = MH_PAUSE_ENTER;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.page == 2);
	input.keys = MH_PAUSE_LEFT;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		settings.effects_volume == 252);
	CHECK(output.display.effects_volume == 254);
	CHECK(output.count == 2 &&
		output.events[1].kind == MH_PAUSE_EFFECTS_VOLUME);
	input.keys = MH_PAUSE_RIGHT;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		settings.effects_volume == 254);
	CHECK(output.count == 3 && output.events[1].value == MH_PAUSE_ERROR);
	settings.effects_volume = settings.cd_volume = 255;
	input.keys = 0;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.display.effects_volume == 255);
	input.keys = MH_PAUSE_LEFT;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		settings.effects_volume == 253);
	state.selected[2] = 1;
	input.keys = MH_PAUSE_RIGHT;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		settings.cd_volume == 254);
	CHECK(output.display.cd_volume == 255 &&
		output.events[1].value == MH_PAUSE_ERROR);
	input.keys = MH_PAUSE_TOGGLE;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		state.page == 0 && state.visible);
	CHECK(output.events[0].kind == MH_PAUSE_PLAY &&
		output.events[0].value == MH_PAUSE_ERROR);
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		!state.visible);
	CHECK(output.dimmed == 0);
	CHECK(output.count == 5 &&
		output.events[4].kind == MH_PAUSE_INPUT_MODE &&
		output.events[4].value == 0);
	saved_state = state;
	saved_settings = settings;
	saved_output = output;
	input.keys = 128;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&settings, &saved_settings, sizeof(settings)) == 0);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	input.keys = MH_PAUSE_TOGGLE;
	settings.distance = -1;
	saved_settings = settings;
	CHECK(mh_pause_update(&state, &settings, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&settings, &saved_settings, sizeof(settings)) == 0);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);

	return failures != 0;
}
