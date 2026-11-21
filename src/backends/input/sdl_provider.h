#ifndef MH_BACKENDS_INPUT_SDL_PROVIDER_H
#define MH_BACKENDS_INPUT_SDL_PROVIDER_H

#include "interfaces/input/backend.h"
#include "interfaces/input/clipboard_backend.h"

/* Process-lifetime descriptor; the host acquires window/events separately. */
const struct mh_input_provider	*mh_sdl_input_provider(void);

/* Event-thread host resume: discard queued input, then notify each sampler. */
void	mh_sdl_input_reset(void);

/*
 * Native immediate-read provider; the host keeps the SDL window system
 * acquired.
 */
const struct mh_clipboard_provider	*mh_sdl_clipboard_provider(void);

#endif
