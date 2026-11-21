#include <stdio.h>
#include <string.h>

#include "backends/input/sdl_devices.h"
#include "engine/core/error.h"
#include "engine/ui/menu_controls.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x,    \
				SDL_GetError());                               \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
drain_events(struct mh_sdl_devices *devices)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
		mh_sdl_devices_event(devices, &event);
}

static void
send_mouse(
	struct mh_sdl_devices *devices, float x, float y, int button, int down)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));

	if (button == 0) {
		event.type = SDL_EVENT_MOUSE_MOTION;
		event.motion.windowID = SDL_GetWindowID(devices->window);
		event.motion.xrel = x;
		event.motion.yrel = y;
	} else {
		event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
				: SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.windowID = SDL_GetWindowID(devices->window);
		event.button.button = (Uint8)button;
	}

	CHECK(SDL_PushEvent(&event));
	drain_events(devices);
}

static void
check_capture(
	const struct mh_keyboard *keyboard, unsigned int code, const char *name)
{
	struct mh_menu_controls_state menu;
	struct mh_menu_controls_input input;
	struct mh_menu_controls_output output;
	struct mh_control_profile profile;
	struct mh_control_layout restored;
	char text[MH_CONTROL_TEXT_CAPACITY];
	size_t size;

	memset(&menu, 0, sizeof(menu));
	memset(&input, 0, sizeof(input));
	memset(&profile, 0, sizeof(profile));
	CHECK(mh_control_layout_read((const unsigned char *)"", 0, NULL,
		&profile.layout, NULL) == MH_ERROR_NONE);
	menu.phase = MH_MENU_CONTROLS_CAPTURE;
	menu.column = MH_MENU_CONTROLS_BINDINGS;
	input.held_count = keyboard->held_count;
	input.first_held = keyboard->held_keys[0];
	CHECK(input.held_count == 1 && input.first_held == code);
	CHECK(mh_menu_controls_update(&menu, &profile, 1, &input, &output, NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(profile.layout.bindings[MH_CONTROL_LEFT] == code &&
		profile.dirty);
	CHECK(strcmp(mh_control_key_name(code), name) == 0);
	CHECK(mh_control_layout_encode(&profile.layout, NULL, text,
		sizeof(text), &size, NULL) == MH_ERROR_NONE);
	CHECK(strstr(text, name) != NULL);
	CHECK(mh_control_layout_read((const unsigned char *)text, size, NULL,
		&restored, NULL) == MH_ERROR_NONE);
	CHECK(restored.bindings[MH_CONTROL_LEFT] == code);
}

static void
send_wheel(struct mh_sdl_devices *devices, float value, int flipped)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.windowID = SDL_GetWindowID(devices->window);
	event.wheel.y = value;
	event.wheel.direction =
		flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
	CHECK(SDL_PushEvent(&event));
	drain_events(devices);
}

static void
check_third_axis(struct mh_sdl_devices *devices, SDL_Joystick *joystick,
	struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT],
	unsigned long *now)
{
	struct mh_device_input input;

	mh_sdl_devices_clear(devices);
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 2, -30000));
	drain_events(devices);
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.joystick_axis_count == 3 && input.joystick[2] == -30000);
	check_capture(keyboard, MH_KEY_JOYSTICK_IN, "JoyIn");
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 2, 30000));
	drain_events(devices);
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	check_capture(keyboard, MH_KEY_JOYSTICK_OUT, "JoyOut");
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 2, 0));
	drain_events(devices);
	send_wheel(devices, -1, 0);
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse_axis_count == 3 && input.mouse[2] == -60);
	check_capture(keyboard, MH_KEY_MOUSE_IN, "MouseIn");
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[2] == -60);
	*now += 101;
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, *now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[2] == 0 && keyboard->held_count == 0);
	send_wheel(devices, 1, 0);
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[2] == 60);
	check_capture(keyboard, MH_KEY_MOUSE_OUT, "MouseOut");
	mh_sdl_devices_clear(devices);
	send_wheel(devices, 1, 1);
	CHECK(mh_sdl_devices_poll(devices, keyboard, down, ++*now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[2] == -60);
	check_capture(keyboard, MH_KEY_MOUSE_IN, "MouseIn");
	mh_sdl_devices_clear(devices);
}

int
main(void)
{
	struct mh_sdl_devices devices;
	struct mh_device_input input;
	struct mh_force_effect forces[MH_FORCE_EFFECTS];
	struct mh_force_status force_status;
	struct mh_keyboard keyboard;
	SDL_VirtualJoystickDesc description;
	SDL_JoystickID id;
	SDL_Joystick *joystick;
	SDL_Window *window;
	SDL_Event event;
	unsigned char down[MH_KEY_COUNT];
	unsigned long now;
	unsigned int i;
	struct mh_error error;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}

	window = SDL_CreateWindow(
		"Motorhead input devices fixture", 320, 200, 0);

	if (window == NULL) {
		fprintf(stderr, "Window: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	if (mh_sdl_devices_init(&devices, window, 0, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "Devices: %s\n", error.message);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	mh_keyboard_init(&keyboard, NULL);
	memset(down, 0, sizeof(down));
	now = 0;

	for (i = 0; i < 100 && !devices.focused; i++) {
		SDL_Delay(10);
		drain_events(&devices);
	}

	CHECK(devices.focused);
	CHECK(!SDL_GetWindowRelativeMouseMode(window));
	CHECK(mh_sdl_mouse_grab(&devices.mouse, 1, NULL) == MH_ERROR_NONE);
	CHECK(SDL_GetWindowRelativeMouseMode(window));
	devices.mouse.motion[0] = 30;
	devices.mouse.previous[0] = 20;
	devices.mouse.buttons[0] = 1;
	CHECK(mh_sdl_mouse_grab(&devices.mouse, 0, NULL) == MH_ERROR_NONE);
	CHECK(!SDL_GetWindowRelativeMouseMode(window));
	CHECK(devices.mouse.motion[0] == 0 && devices.mouse.previous[0] == 0 &&
		devices.mouse.buttons[0] == 0);
	CHECK(mh_sdl_mouse_grab(&devices.mouse, 1, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 5; i++) {
		drain_events(&devices);
		CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1,
			&input, NULL) == MH_ERROR_NONE);
	}

	SDL_INIT_INTERFACE(&description);
	description.name = "Motorhead virtual joystick";
	description.naxes = 3;
	description.nbuttons = 10;
	id = SDL_AttachVirtualJoystick(&description);
	CHECK(id != 0);
	joystick = SDL_OpenJoystick(id);
	CHECK(joystick != NULL);
	drain_events(&devices);
	CHECK(devices.joystick.handle != NULL &&
		SDL_GetJoystickID(devices.joystick.handle) == id);
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 0, -27306));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 1, 27306));
	CHECK(SDL_SetJoystickVirtualButton(joystick, 0, 1));
	CHECK(SDL_SetJoystickVirtualButton(joystick, 7, 1));
	CHECK(SDL_SetJoystickVirtualButton(joystick, 9, 1));
	drain_events(&devices);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.joystick_flags == 1 && input.joystick_button_count == 10);
	CHECK(input.joystick[0] == -27306 && input.joystick[1] == 27306);
	CHECK(!keyboard.held[0xf3] && !keyboard.held[0xf6]);
	CHECK(keyboard.held[0xeb] && keyboard.held[0xf2] &&
		keyboard.held[MH_KEY_ENTER]);
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 0, -27307));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 1, 27307));
	drain_events(&devices);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held[0xf3] && keyboard.held[0xf6] &&
		keyboard.held[MH_KEY_LEFT] && keyboard.held[MH_KEY_DOWN]);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 0, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held[0xf3] && keyboard.held[0xf6] &&
		!keyboard.held[MH_KEY_LEFT] && !keyboard.held[MH_KEY_ENTER]);
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 0, 0));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, 1, 0));

	for (i = 0; i < 10; i++)
		CHECK(SDL_SetJoystickVirtualButton(joystick, (int)i, 0));

	drain_events(&devices);
	mh_sdl_devices_clear(&devices);
	send_mouse(&devices, -51, 53, 0, 0);
	send_mouse(&devices, 0, 0, SDL_BUTTON_LEFT, 1);
	send_mouse(&devices, 0, 0, SDL_BUTTON_RIGHT, 1);
	send_mouse(&devices, 0, 0, SDL_BUTTON_MIDDLE, 1);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse_flags == 1 && input.mouse[0] == -25 &&
		input.mouse[1] == 26);
	CHECK(!keyboard.held[0xf9] && keyboard.held[0xfc] &&
		keyboard.held[MH_KEY_DOWN]);
	CHECK(keyboard.held[0xfd] && keyboard.held[0xfe] &&
		keyboard.held[0xff]);
	CHECK(keyboard.held[MH_KEY_ENTER] && keyboard.held[MH_KEY_ESCAPE]);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 0, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[0] == -25 && input.mouse[1] == 26 &&
		keyboard.held[0xf9]);
	CHECK(!keyboard.held[MH_KEY_ENTER] && !keyboard.held[MH_KEY_LEFT]);
	send_mouse(&devices, 0, 0, SDL_BUTTON_LEFT, 0);
	send_mouse(&devices, 0, 0, SDL_BUTTON_RIGHT, 0);
	send_mouse(&devices, 0, 0, SDL_BUTTON_MIDDLE, 0);
	now += 101;
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[0] == 0 && input.mouse[1] == 0 &&
		keyboard.held_count == 0);
	send_mouse(&devices, .75f, -.75f, 0, 0);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	send_mouse(&devices, 1.25f, -1.25f, 0, 0);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[0] == 1 && input.mouse[1] == -1);
	check_third_axis(&devices, joystick, &keyboard, down, &now);
	send_wheel(&devices, 1, 0);

	/*
	 * Focus loss clears held buttons, fractional motion, previous sample
	 * and menu motion history. Other windows cannot feed this adapter.
	 */
	send_mouse(&devices, 200, 200, 0, 0);
	send_mouse(&devices, 0, 0, SDL_BUTTON_LEFT, 1);
	memset(&event, 0, sizeof(event));
	memset(forces, 0, sizeof(forces));
	CHECK(mh_sdl_feedback_poll(&devices.joystick.feedback,
		devices.joystick.handle, forces, &force_status,
		NULL) == MH_ERROR_NONE);
	CHECK(!force_status.available[0] && !force_status.available[1] &&
		!force_status.available[2] &&
		devices.joystick.feedback.attempted);
	event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	event.window.windowID = SDL_GetWindowID(window);
	mh_sdl_devices_event(&devices, &event);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(!devices.joystick.feedback.attempted &&
		!devices.joystick.feedback.initialized);
	CHECK(!input.mouse_flags && !input.joystick_flags &&
		keyboard.held_count == 0);
	event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
	mh_sdl_devices_event(&devices, &event);
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = SDL_GetWindowID(window) + 1;
	event.motion.xrel = 100;
	mh_sdl_devices_event(&devices, &event);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE);
	CHECK(input.mouse[0] == 0 && input.mouse[2] == 0 &&
		input.mouse_buttons[0] == 0 && keyboard.held_count == 0);
	event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
	mh_sdl_devices_event(&devices, &event);
	CHECK(!devices.focused && !devices.joystick.feedback.attempted);
	event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
	mh_sdl_devices_event(&devices, &event);
	CHECK(devices.focused);
	CHECK(mh_sdl_feedback_poll(&devices.joystick.feedback,
		devices.joystick.handle, forces, &force_status,
		NULL) == MH_ERROR_NONE);
	CHECK(devices.joystick.feedback.attempted);
	CHECK(SDL_DetachVirtualJoystick(id));
	SDL_CloseJoystick(joystick);
	drain_events(&devices);
	CHECK(devices.joystick.handle == NULL);
	CHECK(!devices.joystick.feedback.attempted &&
		!devices.joystick.feedback.initialized);
	CHECK(mh_sdl_devices_poll(&devices, &keyboard, down, ++now, 1, &input,
		NULL) == MH_ERROR_NONE &&
		!input.joystick_flags);
	mh_sdl_devices_close(&devices);
	mh_sdl_devices_close(&devices);
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!failures)
		puts("SDL devices: XYZ capture/save/reload, wheel signs/filtering, hotplug, axis thresholds, buttons, mode aliases focus/background release and non-haptic hotplug cleanup passed");

	return failures != 0;
}
