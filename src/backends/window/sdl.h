#ifndef MH_SDL_WINDOW_H
#define MH_SDL_WINDOW_H

#include <SDL3/SDL.h>

#include "interfaces/window/window.h"

/* Borrowed SDL handle, or NULL for absent/incompatible windows. */
SDL_Window	*mh_sdl_window(const struct mh_window *window);

enum mh_error_code	mh_sdl_display_modes(void *context, unsigned int limit,
	mh_display_mode_visit visit, void *visitor, struct mh_error *error_out);

#endif
