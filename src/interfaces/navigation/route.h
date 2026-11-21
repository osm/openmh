#ifndef MH_NAVIGATION_ROUTE_DATA_H
#define MH_NAVIGATION_ROUTE_DATA_H

#include "interfaces/animation/scene.h"

/*
 * Owned decoded track motion. Key time is in RAW FRAME units (scale 1),
 * not scene seconds. Last key closes the loop. Decoded data must stay
 * unmodified; routes use repeat behavior.
 */
struct mh_route {
	struct mh_scene_object	motion;
};

#endif
