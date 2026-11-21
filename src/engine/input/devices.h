#ifndef MH_INPUT_DEVICES_H
#define MH_INPUT_DEVICES_H

#include "engine/core/error.h"
#include "engine/input/keyboard.h"

#define MH_DEVICE_AXES 3
#define MH_MOUSE_BUTTONS 3
#define MH_JOYSTICK_BUTTONS 8
#define MH_DEVICE_PRESENT 1UL
#define MH_DEVICE_KEYS_MENU 1UL

/*
 * Original device snapshot, before virtual key synthesis. Bit 0 of flags
 * denotes presence. Buttons retain their original words: synthesis accepts
 * exactly 1, whereas the driving binding query accepts any nonzero value.
 * Mouse buttons are left/right/middle; joystick buttons are 1..8.
 * Axis counts gate XYZ virtual keys; axes beyond Z are ignored.
 * Coordinates and calibration are signed 32-bit values. mouse[] contains
 * the filtered relative motion, not absolute cursor coordinates.
 */
struct mh_device_input {
	unsigned long	mouse_flags, joystick_flags, joystick_button_count;
	unsigned long	mouse_axis_count, joystick_axis_count;
	unsigned long mouse_buttons[MH_MOUSE_BUTTONS],
		joystick_buttons[MH_JOYSTICK_BUTTONS];
	long mouse[MH_DEVICE_AXES], joystick[MH_DEVICE_AXES],
		minimum[MH_DEVICE_AXES], maximum[MH_DEVICE_AXES];
};

struct mh_device_keys {
	unsigned long	sample_clock;
	long		mouse[MH_DEVICE_AXES];
};

/*
 * Zero initialization gives the original fresh state. Keyboard reset leaves
 * the accumulated motion intact; its next zero-clock poll resets only the
 * sample clock. The 100ms window expires strictly after 100ms. mode == 1
 * also aliases device keys to menu arrows/Enter/Escape. All modes produce
 * the device scan codes used by Controls remapping. The input/output key
 * arrays may alias. Errors preserve state and output.
 */
enum mh_error_code	mh_device_keys_poll(struct mh_device_keys *state,
	const struct mh_device_input *input,
	const unsigned char down[MH_KEY_COUNT], unsigned long clock,
	int reset_clock, unsigned long mode, unsigned char output[MH_KEY_COUNT],
	struct mh_error *error_out);

/*
 * Original relative mouse filter: truncate the wrapped signed 32-bit sum
 * of the previous and current sample divided by two. Save current sample
 * for next time. raw may alias either destination; previous and output
 * must be distinct. Errors leave both outputs untouched.
 */
enum mh_error_code	mh_mouse_filter_step(long previous[MH_DEVICE_AXES],
	const long raw[MH_DEVICE_AXES], long output[MH_DEVICE_AXES],
	struct mh_error *error_out);

#endif
