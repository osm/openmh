#ifndef MH_MENU_TRIANGLES_H
#define MH_MENU_TRIANGLES_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"

struct mh_menu_point {
	float	x, y;
};

/*
 * Rasterize count consecutive triangles into the padded indexed allocation,
 * relative to canvas.begin. Inclusive spans and floating edge stepping are
 * retained. Unlike sprite rows, triangles do not use end as a clip boundary.
 * Writes are bounded by the allocation. Up to 32 triangles; all coordinates
 * must be finite and within +/-32768. Invalid inputs leave every pixel
 * unchanged.
 */
enum mh_error_code	mh_menu_triangles(const struct mh_menu_point *points,
	unsigned int count, unsigned int color, int no_subpixel,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

#endif
