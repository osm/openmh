#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "client/diagnostics/input.h"
#include "engine/core/error.h"
#include "engine/input/device_choices.h"
#include "interfaces/input/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "platform input line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
next_event(struct mh_input *input, struct mh_event *event)
{
	unsigned int tries;

	/* SDL may end a polling cycle before delivering newly queued events. */
	for (tries = 0; tries < 4; tries++)
		if (input != NULL ? mh_input_event(input, event)
				: mh_input_provider_event(
					mh_sdl_input_provider(), event))
			return 1;

	return 0;
}

int
main(void)
{
	struct mh_input input;
	struct mh_event event;
	struct mh_keyboard keyboard;
	struct mh_device_input devices;
	unsigned char down[MH_KEY_COUNT];
	SDL_Event native;
	unsigned int i;

	memset(&input, 0, sizeof(input));
	memset(down, 0, sizeof(down));
	CHECK(SDL_Init(SDL_INIT_EVENTS));
	CHECK(mh_input_open_from(mh_sdl_input_provider(), &input, NULL, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(!mh_input_focused(&input));
	CHECK(mh_input_open_from(mh_sdl_input_provider(), &input, NULL, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_keyboard_init(&keyboard, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 5; i++)
		CHECK(mh_input_poll(&input, &keyboard, down, i * 10, 1,
			&devices, NULL) == MH_ERROR_NONE);

	CHECK(devices.mouse_flags == 0 && devices.joystick_flags == 0);
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	memset(&native, 0, sizeof(native));
	native.type = SDL_EVENT_KEY_DOWN;
	native.key.windowID = 123;
	native.key.scancode = SDL_SCANCODE_Q;
	native.key.key = SDLK_A;
	native.key.repeat = 1;
	native.key.mod = SDL_KMOD_RSHIFT;
	CHECK(SDL_PushEvent(&native));
	CHECK(next_event(&input, &event));
	CHECK(event.type == MH_EVENT_KEY_DOWN && event.window == 123);
	CHECK(event.scan == MH_KEY_Q && event.shortcut == MH_SHORTCUT_NONE);
	CHECK(event.repeat && event.shift);

	/*
	 * Physical bindings must not follow layout-dependent logical shortcuts.
	 */
	native.type = SDL_EVENT_KEY_UP;
	native.key.scancode = SDL_SCANCODE_A;
	native.key.key = SDLK_Q;
	native.key.repeat = 0;
	native.key.mod = 0;
	CHECK(SDL_PushEvent(&native));
	CHECK(next_event(&input, &event));
	CHECK(event.type == MH_EVENT_KEY_UP && event.scan == MH_KEY_A);
	CHECK(event.shortcut == MH_SHORTCUT_Q && !event.repeat && !event.shift);

	memset(&native, 0, sizeof(native));
	native.type = SDL_EVENT_MOUSE_MOTION;
	native.motion.windowID = 456;
	native.motion.xrel = 1.25f;
	native.motion.yrel = -2.5f;
	native.motion.state = SDL_BUTTON_LMASK;
	CHECK(SDL_PushEvent(&native));
	CHECK(next_event(&input, &event));

	if (mh_input_scan_available(MH_KEY_MOUSE_LEFT)) {
		CHECK(event.type == MH_EVENT_MOUSE_MOTION &&
			event.window == 456);
		CHECK(event.mouse_x == 1.25f && event.mouse_y == -2.5f &&
			event.mouse_left);
	} else
		CHECK(event.type == MH_EVENT_OTHER && event.mouse_x == 0 &&
			!event.mouse_left);

	CHECK(event.scan == 0 && event.shortcut == MH_SHORTCUT_NONE);

	memset(&native, 0, sizeof(native));
	native.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	native.window.windowID = 789;
	CHECK(SDL_PushEvent(&native));
	CHECK(next_event(&input, &event));
	CHECK(event.type == MH_EVENT_FOCUS_LOST && event.window == 789);
	CHECK(event.mouse_x == 0 && !event.mouse_left);

	memset(&native, 0, sizeof(native));
	native.type = SDL_EVENT_USER;
	CHECK(SDL_PushEvent(&native));
	CHECK(next_event(&input, &event));
	CHECK(event.type == MH_EVENT_OTHER && event.window == 0);

	/*
	 * Resume drops stale input while preserving window/quit notifications.
	 */
	native.type = SDL_EVENT_KEY_DOWN;
	CHECK(SDL_PushEvent(&native));
	native.type = SDL_EVENT_MOUSE_MOTION;
	CHECK(SDL_PushEvent(&native));
	native.type = SDL_EVENT_QUIT;
	CHECK(SDL_PushEvent(&native));
	mh_sdl_input_reset();
	mh_sdl_input_reset();
	CHECK(next_event(&input, &event));
	CHECK(event.type == MH_EVENT_INPUT_RESET && event.window == 0);
	CHECK(event.scan == 0 && event.mouse_x == 0 && !event.mouse_left);
	CHECK(next_event(NULL, &event));
	CHECK(event.type == MH_EVENT_QUIT);
	CHECK(!mh_event_poll(&input, &event));
	mh_input_close(&input);
	mh_input_close(&input);
	CHECK(input.backend == NULL);
	CHECK(mh_input_poll(&input, &keyboard, down, 0, 1, &devices, NULL) !=
		MH_ERROR_NONE);
	SDL_Quit();
	puts("Platform input preserves physical keys, shortcuts and event boundaries");

	return 0;
}
