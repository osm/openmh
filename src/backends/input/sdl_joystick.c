#include <string.h>

#include "backends/input/sdl_joystick.h"
#include "engine/core/limits.h"

/* Symmetric calibration endpoint, one above SDL's largest positive sample. */
#define JOYSTICK_AXIS_LIMIT 32768L

enum mh_error_code
mh_sdl_joystick_init(
	struct mh_sdl_joystick *joystick, struct mh_error *error_out)
{
	memset(joystick, 0, sizeof(*joystick));

	if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	joystick->initialized = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sdl_joystick_close(struct mh_sdl_joystick *joystick)
{
	if (MH_INPUT_BUILD_FEEDBACK)
		mh_sdl_feedback_close(&joystick->feedback);

	SDL_CloseJoystick(joystick->handle);

	if (joystick->initialized)
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);

	memset(joystick, 0, sizeof(*joystick));
}

void
mh_sdl_joystick_refresh(struct mh_sdl_joystick *joystick)
{
	SDL_JoystickID *ids;
	int count, i;

	if (joystick->handle != NULL)
		return;

	ids = SDL_GetJoysticks(&count);

	if (ids == NULL)
		return;

	for (i = 0; i < count; i++) {
		joystick->handle = SDL_OpenJoystick(ids[i]);

		if (joystick->handle == NULL)
			continue;

		if (SDL_GetNumJoystickAxes(joystick->handle) > 0)
			break;

		SDL_CloseJoystick(joystick->handle);
		joystick->handle = NULL;
	}

	SDL_free(ids);
}

void
mh_sdl_joystick_event(struct mh_sdl_joystick *joystick, const SDL_Event *event)
{
	if (event->type == SDL_EVENT_JOYSTICK_REMOVED &&
		joystick->handle != NULL &&
		event->jdevice.which == SDL_GetJoystickID(joystick->handle)) {
		if (MH_INPUT_BUILD_FEEDBACK)
			mh_sdl_feedback_close(&joystick->feedback);

		SDL_CloseJoystick(joystick->handle);
		joystick->handle = NULL;
		mh_sdl_joystick_refresh(joystick);
	}

	if (event->type == SDL_EVENT_JOYSTICK_ADDED)
		mh_sdl_joystick_refresh(joystick);
}

void
mh_sdl_joystick_poll(const struct mh_sdl_joystick *joystick,
	struct mh_device_input *snapshot)
{
	unsigned int axis, button;
	int axis_count, button_count;

	for (axis = 0; axis < MH_DEVICE_AXES; axis++) {
		snapshot->minimum[axis] = MH_I16_MIN;
		snapshot->maximum[axis] = JOYSTICK_AXIS_LIMIT;
	}

	if (joystick == NULL || joystick->handle == NULL ||
		!SDL_JoystickConnected(joystick->handle))
		return;

	snapshot->joystick_flags = MH_DEVICE_PRESENT;
	axis_count = SDL_GetNumJoystickAxes(joystick->handle);
	button_count = SDL_GetNumJoystickButtons(joystick->handle);
	snapshot->joystick_axis_count =
		axis_count > 0 ? (unsigned long)axis_count : 0;

	if (button_count < 0)
		button_count = 0;

	snapshot->joystick_button_count = (unsigned long)button_count;

	for (axis = 0; axis < MH_DEVICE_AXES && (int)axis < axis_count; axis++)
		snapshot->joystick[axis] =
			SDL_GetJoystickAxis(joystick->handle, (int)axis);

	for (button = 0;
		button < MH_JOYSTICK_BUTTONS && (int)button < button_count;
		button++)
		snapshot->joystick_buttons[button] =
			SDL_GetJoystickButton(joystick->handle, (int)button)
			? 1
			: 0;
}
