#ifndef MH_INPUT_KEYBOARD_H
#define MH_INPUT_KEYBOARD_H

#include "engine/core/error.h"
#define MH_KEY_COUNT 256
#define MH_KEY_DOWN_BIT 0x80

/* Original scan indices; SDL/OS translation belongs in input backends. */
enum mh_key {
	MH_KEY_ESCAPE = 0x01,
	MH_KEY_TAB = 0x0f,
	MH_KEY_V = 0x2f,
	MH_KEY_F1 = 0x3b,
	MH_KEY_F2 = 0x3c,
	MH_KEY_F3 = 0x3d,
	MH_KEY_F4 = 0x3e,
	MH_KEY_F5 = 0x3f,
	MH_KEY_F6 = 0x40,
	MH_KEY_F7 = 0x41,
	MH_KEY_F8 = 0x42,

	MH_KEY_Q = 0x10,
	MH_KEY_W = 0x11,
	MH_KEY_E = 0x12,
	MH_KEY_LSHIFT = 0x2a,
	MH_KEY_RSHIFT = 0x36,
	MH_KEY_LALT = 0x38,
	MH_KEY_RALT = 0xb8,
	MH_KEY_LCTRL = 0x1d,
	MH_KEY_RCTRL = 0x9d,
	MH_KEY_A = 0x1e,
	MH_KEY_S = 0x1f,
	MH_KEY_D = 0x20,
	MH_KEY_SPACE = 0x39,
	MH_KEY_BACKSPACE = 0x0e,
	MH_KEY_ENTER = 0x1c,
	MH_KEY_H = 0x23,
	MH_KEY_CAPS = 0x3a,
	MH_KEY_UP = 0xc8,
	MH_KEY_LEFT = 0xcb,
	MH_KEY_RIGHT = 0xcd,
	MH_KEY_DOWN = 0xd0,
	MH_KEY_DELETE = 0xd3,
	MH_KEY_HOME = 0xc7,
	MH_KEY_PAGEUP = 0xc9,
	MH_KEY_END = 0xcf,
	MH_KEY_PAGEDOWN = 0xd1,
	MH_KEY_INSERT = 0xd2,
	/* Device scan codes share the keyboard event/repeat arrays. */
	MH_KEY_MOUSE_IN = 0xe9,
	MH_KEY_MOUSE_OUT = 0xea,
	MH_KEY_JOYSTICK_BUTTON_8 = 0xeb,
	MH_KEY_JOYSTICK_BUTTON_7 = 0xec,
	MH_KEY_JOYSTICK_BUTTON_6 = 0xed,
	MH_KEY_JOYSTICK_BUTTON_5 = 0xee,
	MH_KEY_JOYSTICK_BUTTON_4 = 0xef,
	MH_KEY_JOYSTICK_BUTTON_3 = 0xf0,
	MH_KEY_JOYSTICK_BUTTON_2 = 0xf1,
	MH_KEY_JOYSTICK_BUTTON_1 = 0xf2,
	MH_KEY_JOYSTICK_LEFT = 0xf3,
	MH_KEY_JOYSTICK_RIGHT = 0xf4,
	MH_KEY_JOYSTICK_UP = 0xf5,
	MH_KEY_JOYSTICK_DOWN = 0xf6,
	MH_KEY_JOYSTICK_IN = 0xf7,
	MH_KEY_JOYSTICK_OUT = 0xf8,
	MH_KEY_MOUSE_LEFT = 0xf9,
	MH_KEY_MOUSE_RIGHT = 0xfa,
	MH_KEY_MOUSE_UP = 0xfb,
	MH_KEY_MOUSE_DOWN = 0xfc,
	MH_KEY_MOUSE_BUTTON_LEFT = 0xfd,
	MH_KEY_MOUSE_BUTTON_RIGHT = 0xfe,
	MH_KEY_MOUSE_BUTTON_MIDDLE = 0xff
};

struct mh_keyboard {
	/* Repeat delay, interval and timers are milliseconds. */
	unsigned long	delay, interval;
	unsigned long	clock, previous, elapsed;
	unsigned long	pending[MH_KEY_COUNT];
	unsigned char	held[MH_KEY_COUNT], events[MH_KEY_COUNT];
	unsigned char	raw_down[MH_KEY_COUNT];
	unsigned char	held_keys[MH_KEY_COUNT], event_keys[MH_KEY_COUNT];
	unsigned int	held_count, event_count, startup;
	int		caps;
};

/*
 * Native UI timing: immediate press, 400ms initial delay, 100ms repeats. Enter,
 * Escape and Caps Lock never repeat. Reset retains configuration, Caps Lock and
 * physical key state, and discards four polls. Keys held through reset cannot
 * produce a fresh confirmation. A zero configured interval selects the default
 * interval.
 */
enum mh_error_code	mh_keyboard_init(struct mh_keyboard *keyboard,
	struct mh_error *error_out);
void			mh_keyboard_reset(struct mh_keyboard *keyboard);
enum mh_error_code	mh_keyboard_configure(struct mh_keyboard *keyboard,
	unsigned long delay, unsigned long interval,
	struct mh_error *error_out);

/*
 * Read bit 7 of each supplied byte, matching DirectInput's held snapshot.
 * now_ms is an unsigned 32-bit real-time clock, independent of race pause.
 * Events/lists are replaced each poll; timers wrap at 32 bits. A failed
 * call preserves the state. Character translation and device polling are
 * separate. Repeat cadence uses elapsed time, with at most one event per key
 * per poll after a stall; it deliberately replaces original acceleration.
 */
enum mh_error_code	mh_keyboard_poll(struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	struct mh_error *error_out);

struct mh_device_input;
struct mh_device_keys;

/*
 * As above, with original mouse/joystick virtual keys and menu aliases.
 * Synthesis runs before startup suppression, using the same repeat clock.
 * Errors preserve both keyboard and device state.
 */
enum mh_error_code	mh_keyboard_poll_devices(struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	struct mh_device_keys *devices, const struct mh_device_input *input,
	unsigned long mode, struct mh_error *error_out);

#endif
