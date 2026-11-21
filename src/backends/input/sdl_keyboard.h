#ifndef MH_SDL_INPUT_KEYBOARD_H
#define MH_SDL_INPUT_KEYBOARD_H

#include <SDL3/SDL.h>

#include "engine/input/keyboard.h"

/*
 * SDL keyboard layout adapter for the original 256 scan-index byte editor.
 * Zero means an unmapped physical key. Repeat and event ordering are owned
 * by engine/input/keyboard.c, not SDL text-repeat events.
 */
unsigned int	mh_sdl_key_scan(SDL_Scancode scancode);

int	mh_sdl_key_down(void *context, unsigned int scan);
void	mh_sdl_key_characters(void *context,
	const unsigned char down[MH_KEY_COUNT],
	unsigned char characters[MH_KEY_COUNT]);

#endif
