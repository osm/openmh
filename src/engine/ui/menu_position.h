#ifndef MH_MENU_POSITION_H
#define MH_MENU_POSITION_H

#include "engine/core/error.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_POSITION_MIX_MAX 256L

/*
 * Mix zero is settled; 256 is the transition endpoint. Arithmetic right-shift
 * semantics for negative interpolation are explicit. Invalid inputs preserve
 * both output coordinates.
 */
enum mh_error_code	mh_menu_position(const struct mh_menu_layout *layout,
	unsigned int page, unsigned int slot, unsigned int mix, long *x,
	long *y, struct mh_error *error_out);

#endif
