#ifndef MH_MENU_NEEDLE_H
#define MH_MENU_NEEDLE_H

#include "engine/core/error.h"
#include "engine/ui/menu_triangles.h"

#define MH_MENU_NEEDLE_POINTS 6

struct mh_menu_needle {
	float	angle, response;
};

struct mh_menu_needle_draw {
	struct mh_menu_point	points[MH_MENU_NEEDLE_POINTS];
	unsigned int		color;
};

/*
 * Angle units are 4096 per revolution. The retained follower uses elapsed
 * milliseconds, including overshoot; it does not wrap the angle or clamp its
 * response. Main-menu response is 32, length 2.6, width 3 and palette index 1.
 * Invalid input preserves state and output.
 */
enum mh_error_code	mh_menu_needle_init(struct mh_menu_needle *needle,
	float angle, float response, struct mh_error *error_out);
enum mh_error_code	mh_menu_needle_step(struct mh_menu_needle *needle,
	float x, float y, float target, float length, float width,
	unsigned int color, unsigned long elapsed_ms,
	struct mh_menu_needle_draw *output, struct mh_error *error_out);

#endif
