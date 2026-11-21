#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/devices.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_mouse_filter(void)
{
	long previous[MH_DEVICE_AXES], raw[MH_DEVICE_AXES],
		filtered[MH_DEVICE_AXES];

	previous[2] = raw[2] = 0;
	previous[0] = -1;
	previous[1] = MH_I32_MIN;
	raw[0] = -2;
	raw[1] = -1;
	CHECK(mh_mouse_filter_step(previous, raw, filtered, NULL) ==
		MH_ERROR_NONE);
	CHECK(filtered[0] == -1 && filtered[1] == 1073741823L);
	CHECK(previous[0] == -2 && previous[1] == -1);
	CHECK(mh_mouse_filter_step(previous, previous, filtered, NULL) ==
		MH_ERROR_NONE);
	CHECK(filtered[0] == -2 && filtered[1] == -1);
	CHECK(mh_mouse_filter_step(previous, raw, raw, NULL) == MH_ERROR_NONE);
	CHECK(raw[0] == -2 && raw[1] == -1);
	CHECK(mh_mouse_filter_step(NULL, raw, filtered, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	struct mh_device_input input;
	struct mh_device_keys devices, saved_devices;
	struct mh_keyboard keyboard, saved_keyboard;
	unsigned char down[MH_KEY_COUNT], keys[MH_KEY_COUNT],
		saved_keys[MH_KEY_COUNT];
	unsigned int i;

	memset(&input, 0, sizeof(input));
	memset(&devices, 0, sizeof(devices));
	memset(down, 0, sizeof(down));
	mh_keyboard_init(&keyboard, NULL);
	input.mouse_flags = 1;
	input.mouse_axis_count = input.joystick_axis_count = MH_DEVICE_AXES;
	input.mouse[0] = 10;

	for (i = 0; i < 4; i++) {
		CHECK(mh_keyboard_poll_devices(&keyboard, down, i, &devices,
			&input, 1, NULL) == MH_ERROR_NONE);
		CHECK(keyboard.held_count == 0);
	}

	CHECK(devices.mouse[0] == 40);
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 4, &devices, &input, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held[0xfa] && keyboard.held[MH_KEY_RIGHT]);
	input.mouse[0] = 0;
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 99, &devices, &input, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held[0xfa] && !keyboard.held[MH_KEY_RIGHT] &&
		devices.mouse[0] == 50);
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 100, &devices, &input,
		1, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held_count == 0 && devices.mouse[0] == 0 &&
		devices.sample_clock == 101);
	input.mouse[0] = -26;
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 101, &devices, &input,
		1, NULL) == MH_ERROR_NONE);
	mh_keyboard_reset(&keyboard);
	input.mouse[0] = 0;
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 102, &devices, &input,
		1, NULL) == MH_ERROR_NONE);
	CHECK(devices.mouse[0] == -26 && devices.sample_clock == 0 &&
		keyboard.held_count == 0);
	input.mouse_flags = 0;
	input.joystick_flags = 1;
	input.joystick_button_count = 8;
	input.minimum[0] = input.minimum[1] = -100;
	input.maximum[0] = input.maximum[1] = 100;
	input.joystick[0] = -83;
	input.joystick[1] = 83;
	CHECK(mh_device_keys_poll(&devices, &input, down, 1, 0, 1, keys,
		NULL) == MH_ERROR_NONE);
	CHECK(keys[0xf3] == 0 && keys[0xf6] == 0);
	input.joystick[0] = -84;
	input.joystick[1] = 84;
	input.joystick_buttons[0] = 1;
	input.joystick_buttons[1] = 2;
	input.joystick_buttons[7] = 1;
	CHECK(mh_device_keys_poll(&devices, &input, down, 1, 0, 1, keys,
		NULL) == MH_ERROR_NONE);
	CHECK(keys[0xf3] == 128 && keys[0xf6] == 128 && keys[0xeb] == 128);
	CHECK(keys[MH_KEY_LEFT] == 128 && keys[MH_KEY_DOWN] == 128);
	CHECK(keys[MH_KEY_ENTER] == 128 && keys[MH_KEY_ESCAPE] == 0);
	input.joystick_button_count = 0;
	CHECK(mh_device_keys_poll(&devices, &input, down, 1, 0, 0, keys,
		NULL) == MH_ERROR_NONE);
	CHECK(keys[0xf2] == 0 && keys[MH_KEY_LEFT] == 0);

	/*
	 * Original menu aliases OR the virtual byte even if it came from the
	 * supplied physical snapshot, retaining its low bits.
	 */
	down[0xf2] = 0x45;
	down[MH_KEY_ENTER] = 0x12;
	CHECK(mh_device_keys_poll(&devices, &input, down, 1, 0, 1, down,
		NULL) == MH_ERROR_NONE);
	CHECK(down[MH_KEY_ENTER] == 0x57);
	saved_devices = devices;
	saved_keyboard = keyboard;
	memcpy(saved_keys, keys, sizeof(keys));
	CHECK(mh_device_keys_poll(&devices, &input, down, 1, 2, 1, keys,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(keys, saved_keys, sizeof(keys)) == 0);
	CHECK(memcmp(&devices, &saved_devices, sizeof(devices)) == 0);
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 1, NULL, &input, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 1, &devices, NULL, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&keyboard, &saved_keyboard, sizeof(keyboard)) == 0);
#if LONG_MAX > MH_I32_MAX
	input.mouse[1] = 2147483648L;
	CHECK(mh_keyboard_poll_devices(&keyboard, down, 1, &devices, &input, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&keyboard, &saved_keyboard, sizeof(keyboard)) == 0);
	CHECK(memcmp(&devices, &saved_devices, sizeof(devices)) == 0);
#endif
	check_mouse_filter();

	return failures != 0;
}
