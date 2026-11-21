#ifndef MH_INPUT_BINDING_H
#define MH_INPUT_BINDING_H

#include "engine/core/error.h"
#include "engine/input/keyboard.h"

/* Applied word tags are independent; mixed tags retain query priority. */
#define MH_BINDING_MOUSE 0x100UL
#define MH_BINDING_KEYBOARD 0x200UL
#define MH_BINDING_JOYSTICK 0x400UL
/* Direction bits: negative/positive X, Y, Z; buttons follow separately. */
#define MH_BINDING_AXES 3
#define MH_BINDING_DIRECTION_FIRST 0x1000UL
#define MH_BINDING_MOUSE_BUTTON_LEFT 0x100000UL
#define MH_BINDING_MOUSE_BUTTON_MIDDLE 0x200000UL
#define MH_BINDING_MOUSE_BUTTON_RIGHT 0x400000UL
#define MH_BINDING_BUTTON_SHIFT 20
#define MH_BINDING_JOYSTICK_BUTTON_MASK 255UL
#define MH_BINDING_SCAN_MASK 255UL

/* Snapshot button bits differ from the applied binding's left/middle/right. */
enum mh_mouse_button_mask {
	MH_MOUSE_BUTTON_LEFT = 1,
	MH_MOUSE_BUTTON_RIGHT = 2,
	MH_MOUSE_BUTTON_MIDDLE = 4
};

/*
 * Device-independent snapshot at the original query boundary. Keyboard
 * down reads bit 7, events reads nonzero. Mouse buttons are left/right/middle
 * bits 0/1/2; joystick buttons 1..8 are bits 0..7. Axes/calibration use
 * original signed 32-bit units, not SDL coordinates or controller objects.
 */
struct mh_binding_input {
	unsigned char	down[MH_KEY_COUNT], events[MH_KEY_COUNT];
	long		mouse[MH_BINDING_AXES];
	float		mouse_speed[MH_BINDING_AXES];
	unsigned int	mouse_buttons, joystick_buttons;
	long joystick[MH_BINDING_AXES], minimum[MH_BINDING_AXES],
		maximum[MH_BINDING_AXES];
};

/*
 * Check mouse, joystick, then keyboard, testing buttons before directions.
 * repeat selects keyboard events instead of held keys. For compatibility,
 * positive mouse Z uses the negative Y bit. Write amount only on activation.
 * A NULL amount skips analog arithmetic. Errors preserve both outputs.
 *
 * Bindings must fit unsigned 32 bits and coordinates must fit signed 32 bits.
 * Analog queries reject nonfinite sensitivity or output and singular active
 * calibration. Rounding near boundaries depends on long double precision.
 */
enum mh_error_code	mh_binding_query(const struct mh_binding_input *input,
	unsigned long binding, int repeat, unsigned int *active, float *amount,
	struct mh_error *error_out);

#endif
