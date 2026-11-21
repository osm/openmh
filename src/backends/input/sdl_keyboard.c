#include <stddef.h>
#include <string.h>

#include "backends/input/sdl_keyboard.h"
#include "engine/core/charset.h"
#include "interfaces/input/input.h"

struct key_mapping {
	unsigned int	scan;
	SDL_Scancode	scancode;
};
static const struct key_mapping keys[] = {{0x01, SDL_SCANCODE_ESCAPE},
	{0x02, SDL_SCANCODE_1}, {0x03, SDL_SCANCODE_2}, {0x04, SDL_SCANCODE_3},
	{0x05, SDL_SCANCODE_4}, {0x06, SDL_SCANCODE_5}, {0x07, SDL_SCANCODE_6},
	{0x08, SDL_SCANCODE_7}, {0x09, SDL_SCANCODE_8}, {0x0a, SDL_SCANCODE_9},
	{0x0b, SDL_SCANCODE_0}, {0x0c, SDL_SCANCODE_MINUS},
	{0x0d, SDL_SCANCODE_EQUALS}, {0x0e, SDL_SCANCODE_BACKSPACE},
	{0x0f, SDL_SCANCODE_TAB}, {0x10, SDL_SCANCODE_Q},
	{0x11, SDL_SCANCODE_W}, {0x12, SDL_SCANCODE_E}, {0x13, SDL_SCANCODE_R},
	{0x14, SDL_SCANCODE_T}, {0x15, SDL_SCANCODE_Y}, {0x16, SDL_SCANCODE_U},
	{0x17, SDL_SCANCODE_I}, {0x18, SDL_SCANCODE_O}, {0x19, SDL_SCANCODE_P},
	{0x1a, SDL_SCANCODE_LEFTBRACKET}, {0x1b, SDL_SCANCODE_RIGHTBRACKET},
	{0x1c, SDL_SCANCODE_RETURN}, {0x1d, SDL_SCANCODE_LCTRL},
	{0x1e, SDL_SCANCODE_A}, {0x1f, SDL_SCANCODE_S}, {0x20, SDL_SCANCODE_D},
	{0x21, SDL_SCANCODE_F}, {0x22, SDL_SCANCODE_G}, {0x23, SDL_SCANCODE_H},
	{0x24, SDL_SCANCODE_J}, {0x25, SDL_SCANCODE_K}, {0x26, SDL_SCANCODE_L},
	{0x27, SDL_SCANCODE_SEMICOLON}, {0x28, SDL_SCANCODE_APOSTROPHE},
	{0x29, SDL_SCANCODE_GRAVE}, {0x2a, SDL_SCANCODE_LSHIFT},
	{0x2b, SDL_SCANCODE_BACKSLASH}, {0x2c, SDL_SCANCODE_Z},
	{0x2d, SDL_SCANCODE_X}, {0x2e, SDL_SCANCODE_C}, {0x2f, SDL_SCANCODE_V},
	{0x30, SDL_SCANCODE_B}, {0x31, SDL_SCANCODE_N}, {0x32, SDL_SCANCODE_M},
	{0x33, SDL_SCANCODE_COMMA}, {0x34, SDL_SCANCODE_PERIOD},
	{0x35, SDL_SCANCODE_SLASH}, {0x36, SDL_SCANCODE_RSHIFT},
	{0x37, SDL_SCANCODE_KP_MULTIPLY}, {0x38, SDL_SCANCODE_LALT},
	{0x39, SDL_SCANCODE_SPACE}, {0x3a, SDL_SCANCODE_CAPSLOCK},
	{0x3b, SDL_SCANCODE_F1}, {0x3c, SDL_SCANCODE_F2},
	{0x3d, SDL_SCANCODE_F3}, {0x3e, SDL_SCANCODE_F4},
	{0x3f, SDL_SCANCODE_F5}, {0x40, SDL_SCANCODE_F6},
	{0x41, SDL_SCANCODE_F7}, {0x42, SDL_SCANCODE_F8},
	{0x43, SDL_SCANCODE_F9}, {0x44, SDL_SCANCODE_F10},
	{0x45, SDL_SCANCODE_NUMLOCKCLEAR}, {0x46, SDL_SCANCODE_SCROLLLOCK},
	{0x47, SDL_SCANCODE_KP_7}, {0x48, SDL_SCANCODE_KP_8},
	{0x49, SDL_SCANCODE_KP_9}, {0x4a, SDL_SCANCODE_KP_MINUS},
	{0x4b, SDL_SCANCODE_KP_4}, {0x4c, SDL_SCANCODE_KP_5},
	{0x4d, SDL_SCANCODE_KP_6}, {0x4e, SDL_SCANCODE_KP_PLUS},
	{0x4f, SDL_SCANCODE_KP_1}, {0x50, SDL_SCANCODE_KP_2},
	{0x51, SDL_SCANCODE_KP_3}, {0x52, SDL_SCANCODE_KP_0},
	{0x53, SDL_SCANCODE_KP_PERIOD}, {0x56, SDL_SCANCODE_NONUSBACKSLASH},
	{0x57, SDL_SCANCODE_F11}, {0x58, SDL_SCANCODE_F12},
	{0x9c, SDL_SCANCODE_KP_ENTER}, {0x9d, SDL_SCANCODE_RCTRL},
	{0xb5, SDL_SCANCODE_KP_DIVIDE}, {0xb7, SDL_SCANCODE_PRINTSCREEN},
	{0xb8, SDL_SCANCODE_RALT}, {0xc5, SDL_SCANCODE_PAUSE},
	{0xc7, SDL_SCANCODE_HOME}, {0xc8, SDL_SCANCODE_UP},
	{0xc9, SDL_SCANCODE_PAGEUP}, {0xcb, SDL_SCANCODE_LEFT},
	{0xcd, SDL_SCANCODE_RIGHT}, {0xcf, SDL_SCANCODE_END},
	{0xd0, SDL_SCANCODE_DOWN}, {0xd1, SDL_SCANCODE_PAGEDOWN},
	{0xd2, SDL_SCANCODE_INSERT}, {0xd3, SDL_SCANCODE_DELETE},
	{0xdb, SDL_SCANCODE_LGUI}, {0xdc, SDL_SCANCODE_RGUI},
	{0xdd, SDL_SCANCODE_APPLICATION}};

unsigned int
mh_sdl_key_scan(SDL_Scancode scancode)
{
	size_t i;

	/*
	 * Some ISO keyboards report the physical backslash position this way.
	 */
	if (scancode == SDL_SCANCODE_NONUSHASH)
		return 0x2b;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (scancode == keys[i].scancode)
			return keys[i].scan;

	return 0;
}

static SDL_Keycode
keypad_character(
	SDL_Scancode scancode, SDL_Keymod modifiers, SDL_Keycode translated)
{
	if (scancode == SDL_SCANCODE_KP_PLUS)
		return '+';

	if (scancode == SDL_SCANCODE_KP_MINUS)
		return '-';

	if (scancode == SDL_SCANCODE_KP_MULTIPLY)
		return '*';

	if (scancode == SDL_SCANCODE_KP_DIVIDE)
		return '/';

	if (!(modifiers & SDL_KMOD_NUM) || (modifiers & SDL_KMOD_SHIFT))
		return translated;

	if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
		return (SDL_Keycode)('1' + scancode - SDL_SCANCODE_KP_1);

	if (scancode == SDL_SCANCODE_KP_0)
		return '0';

	if (scancode == SDL_SCANCODE_KP_PERIOD)
		return '.';

	return translated;
}

void
mh_sdl_key_characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char characters[MH_KEY_COUNT])
{
	SDL_Keymod modifiers;
	SDL_Keycode keycode;
	size_t i;

	(void)context;
	memset(characters, 0, MH_KEY_COUNT);
	modifiers = SDL_GetModState();

	/*
	 * AltGr layouts may report Ctrl+Right Alt. Ordinary Ctrl/Alt chords do
	 * not insert characters. Layout and lock state are native SDL policy;
	 * the original Win32 ToAscii boundary is not emulated here.
	 */
	if ((modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) &&
		!(modifiers & (SDL_KMOD_RALT | SDL_KMOD_MODE)))
		return;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (!(down[keys[i].scan] & MH_KEY_DOWN_BIT))
			continue;

		keycode = SDL_GetKeyFromScancode(
			keys[i].scancode, modifiers, false);
		keycode =
			keypad_character(keys[i].scancode, modifiers, keycode);
		characters[keys[i].scan] = keycode < 32 || keycode == 127
			? 0
			: mh_windows_1252_character(keycode);
	}
}

int
mh_sdl_key_down(void *context, unsigned int scan)
{
	size_t i;

	(void)context;

	if (scan == 0 || scan >= MH_KEY_COUNT)
		return 0;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (keys[i].scan == scan)
			return SDL_GetKeyboardState(NULL)[keys[i].scancode] !=
				0;

	return 0;
}
