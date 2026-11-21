#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_devices.h"
#include "backends/input/sdl_keyboard.h"
#include "backends/input/sdl_provider.h"
#include "backends/window/sdl.h"
#include "engine/core/error.h"
#include "interfaces/input/backend.h"

struct sdl_input {
	struct mh_sdl_devices	devices;
	unsigned int		generation;
};

static unsigned int reset_generation;

void
mh_sdl_input_reset(void)
{
	/* Keep window, quit and device-removal notifications in the queue. */
	SDL_ResetKeyboard();
	SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_TEXT_INPUT);
	SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
	reset_generation++;
}

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab_mouse,
	void **output, struct mh_error *error_out)
{
	struct sdl_input *input;
	enum mh_error_code status;

	(void)context;

	if (window != NULL && mh_sdl_window(window) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"input requires an SDL window");

	input = calloc(1, sizeof(*input));

	if (input == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening input");

	status = mh_sdl_devices_init(
		&input->devices, mh_sdl_window(window), grab_mouse, error_out);

	if (status != MH_ERROR_NONE) {
		free(input);
		return status;
	}

	input->generation = reset_generation;
	*output = input;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_input(void *context, void *token)
{
	struct sdl_input *input;

	(void)context;
	input = token;
	mh_sdl_devices_close(&input->devices);
	free(input);
}

static int
focused(void *context, const void *token)
{
	const struct sdl_input *input;

	(void)context;
	input = token;

	return input->devices.focused;
}

static enum mh_shortcut
key_shortcut(SDL_Keycode key)
{
	switch (key) {
	case SDLK_ESCAPE:
		return MH_SHORTCUT_ESCAPE;
	case SDLK_RETURN:
		return MH_SHORTCUT_ENTER;
	case SDLK_TAB:
		return MH_SHORTCUT_TAB;
	case SDLK_SPACE:
		return MH_SHORTCUT_SPACE;
	case SDLK_HOME:
		return MH_SHORTCUT_HOME;
	case SDLK_F1:
		return MH_SHORTCUT_F1;
	case SDLK_F2:
		return MH_SHORTCUT_F2;
	case SDLK_F3:
		return MH_SHORTCUT_F3;
	case SDLK_F4:
		return MH_SHORTCUT_F4;
	case SDLK_E:
		return MH_SHORTCUT_E;
	case SDLK_F:
		return MH_SHORTCUT_F;
	case SDLK_G:
		return MH_SHORTCUT_G;
	case SDLK_M:
		return MH_SHORTCUT_M;
	case SDLK_N:
		return MH_SHORTCUT_N;
	case SDLK_P:
		return MH_SHORTCUT_P;
	case SDLK_Q:
		return MH_SHORTCUT_Q;
	case SDLK_R:
		return MH_SHORTCUT_R;
	default:
		return MH_SHORTCUT_NONE;
	}
}

static int
poll_event(void *context, void *token, struct mh_event *event)
{
	struct sdl_input *input;
	SDL_Event native;

	(void)context;
	input = token;

	if (event == NULL)
		return 0;

	if (input != NULL && input->generation != reset_generation) {
		input->generation = reset_generation;
		mh_sdl_devices_clear(&input->devices);
		memset(event, 0, sizeof(*event));
		event->type = MH_EVENT_INPUT_RESET;
		event->window = input->devices.window == NULL
			? 0
			: SDL_GetWindowID(input->devices.window);
		return 1;
	}

	if (!SDL_PollEvent(&native))
		return 0;

	if (input != NULL)
		mh_sdl_devices_event(&input->devices, &native);

	memset(event, 0, sizeof(*event));

	switch (native.type) {
	case SDL_EVENT_QUIT:
		event->type = MH_EVENT_QUIT;
		break;

	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		event->window = native.window.windowID;
		event->type = MH_EVENT_WINDOW_CLOSE;
		break;

	case SDL_EVENT_WINDOW_FOCUS_LOST:
		event->window = native.window.windowID;
		event->type = MH_EVENT_FOCUS_LOST;
		break;

	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		event->window = native.window.windowID;
		event->type = MH_EVENT_FOCUS_GAINED;
		break;

	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		event->type = native.type == SDL_EVENT_KEY_DOWN
			? MH_EVENT_KEY_DOWN
			: MH_EVENT_KEY_UP;
		event->window = native.key.windowID;
		event->scan = mh_sdl_key_scan(native.key.scancode);
		event->shortcut = key_shortcut(native.key.key);
		event->repeat = native.key.repeat != 0;
		event->shift = (native.key.mod & SDL_KMOD_SHIFT) != 0;
		break;

	case SDL_EVENT_MOUSE_MOTION:
		if (!MH_INPUT_BUILD_MOUSE)
			break;

		event->type = MH_EVENT_MOUSE_MOTION;
		event->window = native.motion.windowID;
		event->mouse_x = native.motion.xrel;
		event->mouse_y = native.motion.yrel;
		event->mouse_left =
			(native.motion.state & SDL_BUTTON_LMASK) != 0;
		break;

	case SDL_EVENT_MOUSE_WHEEL:
		if (!MH_INPUT_BUILD_MOUSE)
			break;

		event->type = MH_EVENT_MOUSE_WHEEL;
		event->window = native.wheel.windowID;
		event->wheel_y = native.wheel.y;
		break;

	default:
		/* Consume unhandled SDL events as MH_EVENT_OTHER. */
		break;
	}

	return 1;
}

static enum mh_error_code
poll_devices(void *context, void *token, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	struct sdl_input *input;

	(void)context;
	input = token;

	return mh_sdl_devices_poll(&input->devices, keyboard, down, now_ms,
		mode, snapshot, error_out);
}

#if MH_INPUT_BUILD_FEEDBACK
static enum mh_error_code
poll_force(void *context, void *token,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out)
{
	struct sdl_input *input;

	(void)context;
	input = token;

	if (!input->devices.focused) {
		mh_sdl_feedback_close(&input->devices.joystick.feedback);
		memset(output, 0, sizeof(*output));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_sdl_feedback_poll(&input->devices.joystick.feedback,
		input->devices.joystick.handle, initial, output, error_out);
}

static enum mh_error_code
apply_force(void *context, void *token,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out)
{
	struct sdl_input *input;

	(void)context;
	input = token;

	if (!input->devices.focused)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_sdl_feedback_apply(&input->devices.joystick.feedback, effects,
		changed, start, error_out);
}

static void
stop_force(void *context, void *token)
{
	struct sdl_input *input;

	(void)context;
	input = token;
	mh_sdl_feedback_close(&input->devices.joystick.feedback);
}

static const struct mh_force_ops force_ops = {
	poll_force, apply_force, stop_force};

#define INPUT_FORCE_OPS (&force_ops)
#else
#define INPUT_FORCE_OPS NULL
#endif

static enum mh_error_code
mouse_grab(void *context, void *token, int enabled, struct mh_error *error_out)
{
	struct sdl_input *input;

	(void)context;
	input = token;

	if (MH_INPUT_BUILD_MOUSE)
		return mh_sdl_mouse_grab(
			&input->devices.mouse, enabled, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_input_ops input_ops = {open_input, close_input, focused,
	poll_event, poll_devices, mh_sdl_key_down, mh_sdl_key_characters,
	INPUT_FORCE_OPS, mouse_grab};

const struct mh_input_provider *
mh_sdl_input_provider(void)
{
	static const struct mh_input_provider provider = {&input_ops, NULL};

	return &provider;
}
