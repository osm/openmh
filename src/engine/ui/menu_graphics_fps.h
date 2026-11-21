#ifndef MH_MENU_GRAPHICS_FPS_H
#define MH_MENU_GRAPHICS_FPS_H

#include "engine/ui/menu_graphics.h"

/* Private numeric editor, independent of original graphics choice handling. */
int	mh_menu_graphics_fps_valid(const struct mh_menu_graphics_state *state);

/* Called on staged, validated state. Returns 1 when it consumes navigation. */
int	mh_menu_graphics_fps_update(struct mh_menu_graphics_state *state,
	struct mh_av_settings *settings,
	const struct mh_menu_graphics_input *input,
	struct mh_menu_output *output);

#endif
