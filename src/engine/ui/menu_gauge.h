#ifndef MH_MENU_GAUGE_H
#define MH_MENU_GAUGE_H

#include "engine/core/error.h"
#include "engine/ui/menu_triangles.h"

#define MH_MENU_GAUGE_TRIANGLES 8

struct mh_menu_gauge_draw {
	struct mh_menu_point	points[MH_MENU_GAUGE_TRIANGLES * 3];
	unsigned int		count, color;
};

/*
 * Elliptical fan used by the three car statistics. value is not clamped to
 * 0..1: original smoothing can overshoot. X/Y within +/-16384, radius 0..16384,
 * value +/-16; color 0..255. Failure preserves output. Submit count triangles
 * to mh_menu_triangles, retaining their order.
 */
enum mh_error_code	mh_menu_gauge(float x, float y, float radius,
	float value, unsigned int color, struct mh_menu_gauge_draw *output,
	struct mh_error *error_out);

#endif
