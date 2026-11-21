#include <string.h>

#include "backends/input/sdl_devices.h"

/* SDL 64-bit flag macros expand to ULL literals on 32-bit targets. */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#endif
static const SDL_WindowFlags window_input_focus = SDL_WINDOW_INPUT_FOCUS;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void
mh_sdl_devices_clear(struct mh_sdl_devices *devices)
{
	if (devices == NULL)
		return;

	memset(&devices->virtual_keys, 0, sizeof(devices->virtual_keys));

	if (MH_INPUT_BUILD_MOUSE)
		mh_sdl_mouse_clear(&devices->mouse);
}

void
mh_sdl_devices_close(struct mh_sdl_devices *devices)
{
	if (devices == NULL)
		return;

	if (MH_INPUT_BUILD_MOUSE)
		mh_sdl_mouse_close(&devices->mouse);

	if (MH_INPUT_BUILD_JOYSTICK)
		mh_sdl_joystick_close(&devices->joystick);

	memset(devices, 0, sizeof(*devices));
}

enum mh_error_code
mh_sdl_devices_init(struct mh_sdl_devices *devices, SDL_Window *window,
	int grab_mouse, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (devices == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SDL device state");

	memset(devices, 0, sizeof(*devices));

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = MH_ERROR_NONE;

	if (MH_INPUT_BUILD_JOYSTICK)
		status = mh_sdl_joystick_init(&devices->joystick, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	devices->window = window;
	devices->focused =
		(SDL_GetWindowFlags(window) & window_input_focus) != 0;

	if (MH_INPUT_BUILD_MOUSE)
		status = mh_sdl_mouse_init(
			&devices->mouse, window, grab_mouse, error_out);

	if (status != MH_ERROR_NONE) {
		mh_sdl_devices_close(devices);
		return status;
	}

	if (MH_INPUT_BUILD_JOYSTICK)
		mh_sdl_joystick_refresh(&devices->joystick);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sdl_devices_event(struct mh_sdl_devices *devices, const SDL_Event *event)
{
	SDL_WindowID window;

	if (devices == NULL || devices->window == NULL || event == NULL)
		return;

	window = SDL_GetWindowID(devices->window);

	if (MH_INPUT_BUILD_JOYSTICK)
		mh_sdl_joystick_event(&devices->joystick, event);

	if ((event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
		event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) &&
		event->window.windowID == window) {
		mh_sdl_devices_clear(devices);
		devices->focused = event->type == SDL_EVENT_WINDOW_FOCUS_GAINED;

		if (!devices->focused)
			if (MH_INPUT_BUILD_FEEDBACK)
				mh_sdl_feedback_close(
					&devices->joystick.feedback);
	}

	if (event->type == SDL_EVENT_QUIT ||
		event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
		(event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
			event->window.windowID == window)) {
		if (MH_INPUT_BUILD_FEEDBACK)
			mh_sdl_feedback_close(&devices->joystick.feedback);

		devices->focused = 0;
	}

	if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
		devices->focused = (SDL_GetWindowFlags(devices->window) &
					window_input_focus) != 0;

	if (!devices->focused)
		return;

	if (MH_INPUT_BUILD_MOUSE)
		mh_sdl_mouse_event(&devices->mouse, event);
}

enum mh_error_code
mh_sdl_devices_poll(struct mh_sdl_devices *devices,
	struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT],
	unsigned long now_ms, unsigned long mode,
	struct mh_device_input *snapshot, struct mh_error *error_out)
{
	struct mh_device_input next;
	unsigned int axis;
	enum mh_error_code status;

	if (devices == NULL || snapshot == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SDL device poll output");

	memset(&next, 0, sizeof(next));

	for (axis = 0; axis < MH_DEVICE_AXES; axis++) {
		next.minimum[axis] = -32768L;
		next.maximum[axis] = 32768L;
	}

	if (MH_INPUT_BUILD_MOUSE && devices->window != NULL &&
		devices->focused) {
		status = mh_sdl_mouse_poll(&devices->mouse, &next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (MH_INPUT_BUILD_JOYSTICK)
		mh_sdl_joystick_poll(devices->window != NULL && devices->focused
				? &devices->joystick
				: NULL,
			&next);

	status = mh_keyboard_poll_devices(keyboard, down, now_ms,
		&devices->virtual_keys, &next, mode, error_out);

	if (status == MH_ERROR_NONE)
		*snapshot = next;

	return status;
}
