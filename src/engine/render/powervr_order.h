#ifndef MH_POWERVR_ORDER_H
#define MH_POWERVR_ORDER_H

#include <stddef.h>

#include "engine/render/hidden_weather.h"

enum {
	MH_POWERVR_REGION_SIZE = 32,
	MH_POWERVR_WINDINGS = 2
};

#define MH_POWERVR_NO_ITEM ((size_t)-1)

struct mh_powervr_order_vertex {
	float	xy[2], w;
};

struct mh_powervr_order_triangle {
	size_t		call, first, next;
	unsigned int	left, top, right, bottom, winding;
	float		depth;
};

struct mh_powervr_order_set {
	size_t	call, first, last, previous, next;
	float	depth;
};

/*
 * Original PowerSGL region coverage and minimum inverse-depth key. Inputs use
 * finite projected XY and supplied projection W, positive viewport dimensions.
 * Optional water uses phase/scale/frequency; dimensions come from the viewport.
 * Return zero for empty/offscreen/invalid-W triangles. Other output fields may
 * change on rejection; call/first tags remain untouched.
 */
int	mh_powervr_order_bounds(
	const struct mh_powervr_order_vertex vertices[3], unsigned int width,
	unsigned int height, const struct mh_water_frame *water,
	struct mh_powervr_order_triangle *triangle);

/*
 * Group intersecting triangles by original call and winding, then sort the
 * sets by depth with the SDK's traversal and equal-depth insertion rules.
 * Requires two scratch sets per call, and each triangle's call < call_count.
 * Returns the first set or NO_ITEM. Follow set.next and triangle.next; links
 * are rebuilt on each region call. No allocation or renderer API state.
 */
size_t	mh_powervr_order_region(struct mh_powervr_order_triangle *triangles,
	size_t triangle_count, struct mh_powervr_order_set *sets,
	size_t call_count, unsigned int x, unsigned int y);

#endif
