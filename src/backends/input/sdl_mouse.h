#ifndef MH_SDL_MOUSE_H
#define MH_SDL_MOUSE_H

#include <SDL3/SDL.h>

#include "engine/input/devices.h"

struct mh_sdl_mouse {
	/* Borrowed window; close releases relative mouse mode. */
	SDL_Window	*window;

	/* Retain fractional motion until a whole original unit exists. */
	double		motion[MH_DEVICE_AXES];

	/* Previous unfiltered integer sample for the original mouse filter. */
	long		previous[MH_DEVICE_AXES];
	unsigned long	buttons[MH_MOUSE_BUTTONS];
};

enum mh_error_code	mh_sdl_mouse_init(struct mh_sdl_mouse *mouse,
	SDL_Window *window, int grab_mouse, struct mh_error *error_out);
enum mh_error_code	mh_sdl_mouse_grab(struct mh_sdl_mouse *mouse,
	int enabled, struct mh_error *error_out);
void			mh_sdl_mouse_close(struct mh_sdl_mouse *mouse);
void			mh_sdl_mouse_clear(struct mh_sdl_mouse *mouse);

/* The coordinator forwards events only while its window is focused. */
void			mh_sdl_mouse_event(struct mh_sdl_mouse *mouse,
	const SDL_Event *event);
enum mh_error_code	mh_sdl_mouse_poll(struct mh_sdl_mouse *mouse,
	struct mh_device_input *snapshot, struct mh_error *error_out);

#endif
