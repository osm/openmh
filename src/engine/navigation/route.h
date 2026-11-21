#ifndef MH_NAVIGATION_ROUTE_H
#define MH_NAVIGATION_ROUTE_H

#include "engine/animation/scene.h"
#include "interfaces/navigation/route.h"

/* Release owned decoded arrays and clear the owner. */
void	mh_route_free(struct mh_route *route);

#endif
