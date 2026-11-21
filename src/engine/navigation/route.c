#include <stdlib.h>
#include <string.h>

#include "engine/navigation/route.h"

void
mh_route_free(struct mh_route *route)
{
	free(route->motion.keys);
	memset(route, 0, sizeof(*route));
}
