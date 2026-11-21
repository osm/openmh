#ifndef MH_TRON_DRAW_H
#define MH_TRON_DRAW_H

#include "engine/ui/hud.h"
#include "engine/ui/tron_intro.h"

/*
 * Append the original TRON introduction using the hardware font's default
 * unit scale, zero slant/shadow and variable advances. Glyphs keep original
 * projected coordinates/fade, with native atlas UVs and GPU clipping.
 * Failure restores the published list count. No timer advancement here.
 */
int	mh_tron_intro_draw(const struct mh_hud_assets *assets,
	const struct mh_tron_intro *frame, struct mh_ui_list *list);

#endif
