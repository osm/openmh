#include <string.h>

#include "backends/input/sdl_mouse.h"
#include "engine/core/limits.h"

/* Original Windows wheel units per SDL scroll step. */
#define MOUSE_WHEEL_STEP 120.0

void
mh_sdl_mouse_clear(struct mh_sdl_mouse *mouse)
{
	memset(mouse->motion, 0, sizeof(mouse->motion));
	memset(mouse->previous, 0, sizeof(mouse->previous));
	memset(mouse->buttons, 0, sizeof(mouse->buttons));
}

enum mh_error_code
mh_sdl_mouse_init(struct mh_sdl_mouse *mouse, SDL_Window *window,
	int grab_mouse, struct mh_error *error_out)
{
	memset(mouse, 0, sizeof(*mouse));

	mouse->window = window;

	return mh_sdl_mouse_grab(mouse, grab_mouse, error_out);
}

enum mh_error_code
mh_sdl_mouse_grab(
	struct mh_sdl_mouse *mouse, int enabled, struct mh_error *error_out)
{
	if (mouse->window == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!SDL_SetWindowRelativeMouseMode(mouse->window, enabled != 0))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot change mouse grab");

	/* Do not carry filtered motion or held buttons across a mode change. */
	mh_sdl_mouse_clear(mouse);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sdl_mouse_close(struct mh_sdl_mouse *mouse)
{
	if (mouse->window != NULL)
		SDL_SetWindowRelativeMouseMode(mouse->window, 0);

	memset(mouse, 0, sizeof(*mouse));
}

static double
accumulate_motion(double pending, double delta)
{
	double sum;

	/*
	 * Native boundary guard: malformed/nonfinite events must not cause an
	 * out-of-range float-to-integer conversion or poison future samples.
	 */
	if (!(delta >= -(double)MH_U32_SIGN_BIT && delta <= (double)MH_I32_MAX))
		return pending;

	sum = pending + delta;

	if (sum < -(double)MH_U32_SIGN_BIT)
		return -(double)MH_U32_SIGN_BIT;

	if (sum > (double)MH_I32_MAX)
		return (double)MH_I32_MAX;

	return sum;
}

static int
button_index(Uint8 button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:
		return 0;
	case SDL_BUTTON_RIGHT:
		return 1;
	case SDL_BUTTON_MIDDLE:
		return 2;
	default:
		return -1;
	}
}

void
mh_sdl_mouse_event(struct mh_sdl_mouse *mouse, const SDL_Event *event)
{
	SDL_WindowID window;
	int button;
	double wheel;

	window = SDL_GetWindowID(mouse->window);

	if (event->type == SDL_EVENT_MOUSE_MOTION &&
		event->motion.windowID == window) {
		mouse->motion[0] =
			accumulate_motion(mouse->motion[0], event->motion.xrel);
		mouse->motion[1] =
			accumulate_motion(mouse->motion[1], event->motion.yrel);
	}

	if (event->type == SDL_EVENT_MOUSE_WHEEL &&
		event->wheel.windowID == window) {
		wheel = (double)event->wheel.y * MOUSE_WHEEL_STEP;

		if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
			wheel = -wheel;

		mouse->motion[2] = accumulate_motion(mouse->motion[2], wheel);
	}

	if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
		event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
		event->button.windowID == window) {
		button = button_index(event->button.button);

		if (button >= 0)
			mouse->buttons[button] =
				event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
	}
}

enum mh_error_code
mh_sdl_mouse_poll(struct mh_sdl_mouse *mouse, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	long raw[MH_DEVICE_AXES];
	unsigned int axis;
	enum mh_error_code status;

	snapshot->mouse_flags = SDL_HasMouse() ? MH_DEVICE_PRESENT : 0;

	/* SDL supplies XYZ events without a per-mouse axis capability query. */
	snapshot->mouse_axis_count = snapshot->mouse_flags ? MH_DEVICE_AXES : 0;

	for (axis = 0; axis < MH_DEVICE_AXES; axis++) {
		raw[axis] = (long)mouse->motion[axis];
		mouse->motion[axis] -= raw[axis];
	}

	status = mh_mouse_filter_step(
		mouse->previous, raw, snapshot->mouse, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(snapshot->mouse_buttons, mouse->buttons,
		sizeof(snapshot->mouse_buttons));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
