#ifndef MH_SDL_FEEDBACK_H
#define MH_SDL_FEEDBACK_H

#include <SDL3/SDL.h>

#include "interfaces/input/force.h"

struct mh_sdl_feedback {
	/* Owns the haptic handle and effect IDs; borrows the joystick. */
	SDL_Haptic		*device;
	SDL_HapticEffectID	effects[MH_FORCE_EFFECTS];
	Uint64			started[MH_FORCE_EFFECTS];
	unsigned long		duration[MH_FORCE_EFFECTS], generation;
	int			playing[MH_FORCE_EFFECTS];

	/* Retain the subsystem acquisition until close, even if open fails. */
	int			initialized;

	/* Retry a lazy open only after close clears this flag. */
	int			attempted;
	Uint32			features;
};

void			mh_sdl_feedback_close(struct mh_sdl_feedback *feedback);
enum mh_error_code	mh_sdl_feedback_poll(struct mh_sdl_feedback *feedback,
	SDL_Joystick *joystick,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out);
enum mh_error_code	mh_sdl_feedback_apply(struct mh_sdl_feedback *feedback,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out);

#endif
