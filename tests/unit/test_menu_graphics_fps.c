#include <stdio.h>
#include <string.h>

#include "engine/ui/menu_graphics.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "graphics FPS:%d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_graphics_state state;
static struct mh_av_settings settings;
static struct mh_menu_graphics_catalog catalog;
static struct mh_menu_output output;

static enum mh_error_code
key(unsigned int code, unsigned char character)
{
	struct mh_menu_graphics_input input;

	memset(&input, 0, sizeof(input));
	input.keys[code] = 1;
	input.characters[code] = character;
	input.escape = code == MH_KEY_ESCAPE;
	input.up = code == MH_KEY_UP;
	input.down = code == MH_KEY_DOWN;

	return mh_menu_graphics_update(
		&state, &settings, &catalog, &input, &output, NULL);
}

static int
type(const char *text)
{
	while (*text != 0) {
		CHECK(key(2, (unsigned char)*text++) == MH_ERROR_NONE);
	}

	return 0;
}

int
main(void)
{
	struct mh_menu_graphics_device device;
	struct mh_menu_graphics_mode mode;
	struct mh_menu_graphics_state saved;
	struct mh_menu_output saved_output;
	struct mh_menu_graphics_input input;
	unsigned int i;

	memset(&device, 0, sizeof(device));
	mode.width = 640;
	mode.height = 480;
	device.modes = &mode;
	device.count = 1;
	catalog.devices = &device;
	catalog.count = catalog.presentation = 1;
	catalog.screenshots = 1;
	CHECK(mh_av_settings_init(&settings, 255, 255, NULL) == MH_ERROR_NONE);
	state.selected = MH_MENU_GRAPHICS_MAX_FPS;
	settings.vsync = 1;
	settings.max_fps = 200;

	/*
	 * Arbitrary limits work with VSync; a draft never changes the setting.
	 */
	CHECK(type("237") == 0);
	CHECK(state.fps_editing && strcmp(state.fps_text, "237") == 0);
	CHECK(settings.max_fps == 200);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(!state.fps_editing && settings.max_fps == 237 &&
		settings.vsync == 1);

	/* Digits from the translated keypad follow the same editing path. */
	CHECK(key(0x52, '0') == MH_ERROR_NONE);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(settings.max_fps == 0);
	CHECK(type("1001") == 0);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(settings.max_fps == 1001);

	/*
	 * Backspace edits the existing number; Escape cancels without leaving.
	 */
	CHECK(key(MH_KEY_BACKSPACE, 0) == MH_ERROR_NONE);
	CHECK(strcmp(state.fps_text, "100") == 0);
	CHECK(type("9") == 0);
	CHECK(strcmp(state.fps_text, "1009") == 0);
	CHECK(key(MH_KEY_ESCAPE, 0) == MH_ERROR_NONE);
	CHECK(!state.fps_editing && settings.max_fps == 1001);
	CHECK(output.count == 1 && output.events[0].kind == MH_MENU_SOUND);
	CHECK(key(MH_KEY_ESCAPE, 0) == MH_ERROR_NONE);
	CHECK(output.count == 2 && output.events[1].kind == MH_MENU_PAGE);

	/*
	 * Clearing is a draft, not a request to remove the cap. Type 0 for
	 * that.
	 */
	CHECK(key(MH_KEY_DELETE, 0) == MH_ERROR_NONE);
	CHECK(state.fps_text[0] == 0 && settings.max_fps == 1001);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(!state.fps_editing && settings.max_fps == 1001);

	/*
	 * Out-of-range digits and nonnumeric text are rejected, never clamped.
	 */
	CHECK(type("10000") == 0);
	CHECK(type("0") == 0);
	CHECK(strcmp(state.fps_text, "10000") == 0);
	CHECK(output.count == 1 &&
		output.events[0].value == MH_MENU_SOUND_TEXT_REJECT);
	CHECK(type("a.-") == 0);
	CHECK(strcmp(state.fps_text, "10000") == 0);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(settings.max_fps == MH_AV_MAX_FPS);

	/*
	 * Leaving the row accepts a number; leaving an empty draft retains it.
	 */
	CHECK(type("277") == 0);
	CHECK(key(MH_KEY_DOWN, 0) == MH_ERROR_NONE);
	CHECK(settings.max_fps == 277 && !state.fps_editing);
	CHECK(state.selected == MH_MENU_GRAPHICS_SHOW_FPS);
	CHECK(key(MH_KEY_UP, 0) == MH_ERROR_NONE);
	CHECK(key(MH_KEY_DELETE, 0) == MH_ERROR_NONE);
	CHECK(key(MH_KEY_UP, 0) == MH_ERROR_NONE);
	CHECK(settings.max_fps == 277 &&
		state.selected == MH_MENU_GRAPHICS_VSYNC);
	CHECK(key(MH_KEY_DOWN, 0) == MH_ERROR_NONE);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE);
	CHECK(state.fps_editing && strcmp(state.fps_text, "277") == 0);
	CHECK(key(MH_KEY_DELETE, 0) == MH_ERROR_NONE);
	CHECK(type("0") == 0);
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_NONE && settings.max_fps == 0);

	/* Several simultaneous keys cannot overflow the menu's event array. */
	memset(&input, 0, sizeof(input));

	for (i = 32; i < 128; i++) {
		input.keys[i] = 1;
		input.characters[i] = '9';
	}

	CHECK(mh_menu_graphics_update(&state, &settings, &catalog, &input,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 && strcmp(state.fps_text, "9999") == 0);

	/*
	 * Malformed editor state is rejected atomically, including missing NUL.
	 */
	memset(state.fps_text, '9', sizeof(state.fps_text));
	saved = state;
	saved_output = output;
	CHECK(key(MH_KEY_ENTER, 0) == MH_ERROR_INVALID);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(settings.max_fps == 0);

	return 0;
}
