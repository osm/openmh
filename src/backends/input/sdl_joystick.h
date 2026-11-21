#ifndef MH_SDL_JOYSTICK_H
#define MH_SDL_JOYSTICK_H

#include "backends/input/sdl_feedback.h"
#include "engine/input/devices.h"

/* Wheels use the same axes/buttons and optional force output as joysticks. */
struct mh_sdl_joystick {
	/* Owns the selected handle; release feedback before closing it. */
	SDL_Joystick		*handle;
	struct mh_sdl_feedback	feedback;

	/* Owns one joystick subsystem acquisition while initialized. */
	int			initialized;
};

/* Acquire the subsystem, then refresh after the other input setup succeeds. */
enum mh_error_code	mh_sdl_joystick_init(struct mh_sdl_joystick *joystick,
	struct mh_error *error_out);
void			mh_sdl_joystick_close(struct mh_sdl_joystick *joystick);

/* Keep the selected device, or select the first openable one with an axis. */
void	mh_sdl_joystick_refresh(struct mh_sdl_joystick *joystick);
void	mh_sdl_joystick_event(struct mh_sdl_joystick *joystick,
	const SDL_Event *event);

/* NULL supplies calibration only, for unfocused/inert input snapshots. */
void	mh_sdl_joystick_poll(const struct mh_sdl_joystick *joystick,
	struct mh_device_input *snapshot);

#endif
