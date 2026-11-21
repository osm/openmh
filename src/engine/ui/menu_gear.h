#ifndef MH_MENU_GEAR_H
#define MH_MENU_GEAR_H

#include "engine/core/error.h"
#include "engine/ui/menu_lines.h"

/*
 * Six lines per tooth, in original submission order. Aspect and sine precision
 * differ from the selector needle. No raster or RNG side effects; output and
 * count remain unchanged on error.
 */
enum mh_error_code	mh_menu_gear(long x, long y, unsigned int teeth,
	float radius, float angle, unsigned int brightness,
	struct mh_menu_line *lines, unsigned int capacity, unsigned int *count,
	struct mh_error *error_out);

#endif
