#ifndef MH_SDL_INPUT_DEVICES_H
#define MH_SDL_INPUT_DEVICES_H

#include <SDL3/SDL.h>

#include "backends/input/sdl_joystick.h"
#include "backends/input/sdl_mouse.h"
#include "engine/core/error.h"
#include "engine/input/devices.h"

/* One window's focus, device owners and portable virtual-key history. */
struct mh_sdl_devices {
	/* Borrowed from the window backend; must outlive this adapter. */
	SDL_Window		*window;
	struct mh_sdl_mouse	mouse;
	struct mh_sdl_joystick	joystick;
	struct mh_device_keys	virtual_keys;
	int			focused;
};

/* NULL window creates an inert adapter for deterministic captures. */
enum mh_error_code	mh_sdl_devices_init(struct mh_sdl_devices *devices,
	SDL_Window *window, int grab_mouse, struct mh_error *error_out);
void			mh_sdl_devices_close(struct mh_sdl_devices *devices);
void			mh_sdl_devices_clear(struct mh_sdl_devices *devices);
void			mh_sdl_devices_event(struct mh_sdl_devices *devices,
	const SDL_Event *event);

/*
 * Publish snapshot only on success. Sampling may consume mouse motion/history
 * before a later keyboard synthesis failure.
 */
enum mh_error_code	mh_sdl_devices_poll(struct mh_sdl_devices *devices,
	struct mh_keyboard *keyboard, const unsigned char down[MH_KEY_COUNT],
	unsigned long now_ms, unsigned long mode,
	struct mh_device_input *snapshot, struct mh_error *error_out);

#endif
