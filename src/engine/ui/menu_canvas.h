#ifndef MH_MENU_CANVAS_H
#define MH_MENU_CANVAS_H

#include <stddef.h>

#define MH_MENU_CANVAS_BYTE_LIMIT 16777216UL
#define MH_MENU_CANVAS_PITCH_LIMIT 16384

/* Borrows indexed pixels; the caller owns the padded allocation. */
struct mh_menu_canvas {
	unsigned char	*pixels;

	/* Allocation bounds and original row-origin clip range. */
	size_t		size, begin, end;
	unsigned int	pitch;
};

#endif
