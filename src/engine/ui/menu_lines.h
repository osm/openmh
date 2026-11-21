#ifndef MH_MENU_LINES_H
#define MH_MENU_LINES_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"

struct mh_menu_line {
	long		x1, y1, x2, y2;
	unsigned int	brightness;
};

/*
 * Ordered integer clipping, fixed-point edge stepping, saturating addition to
 * palette indices. Nonzero lines omit the final endpoint; a zero-length line
 * draws one pixel. Clip rectangle is [0,width)x[0,height) relative to
 * canvas.begin. Coordinates +/-1e9, dimensions <=16384. Invalid arguments leave
 * all pixels unchanged.
 */
enum mh_error_code	mh_menu_lines(const struct mh_menu_line *lines,
	unsigned int count, unsigned int width, unsigned int height,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * Same original rasterizer with an inclusive clip rectangle relative to
 * canvas.begin. Bounds +/-16384, width <= pitch; padding may be included.
 * The car page uses a 401x401 rectangle around its outline center.
 */
enum mh_error_code	mh_menu_lines_rect(const struct mh_menu_line *lines,
	unsigned int count, long left, long top, long right, long bottom,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
