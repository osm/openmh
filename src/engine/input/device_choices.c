#include "engine/input/device_choices.h"
#include "engine/input/keyboard.h"

int
mh_input_scan_available(unsigned long scan)
{
	if (scan >= MH_KEY_COUNT)
		return 1;

	if (scan >= MH_KEY_JOYSTICK_BUTTON_8 && scan <= MH_KEY_JOYSTICK_OUT)
		return MH_INPUT_BUILD_JOYSTICK;

	if (scan == MH_KEY_MOUSE_IN || scan == MH_KEY_MOUSE_OUT ||
		scan >= MH_KEY_MOUSE_LEFT)
		return MH_INPUT_BUILD_MOUSE;

	return MH_INPUT_BUILD_KEYBOARD;
}
