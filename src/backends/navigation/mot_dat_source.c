#include <stddef.h>

#include "backends/navigation/internal.h"
#include "backends/navigation/provider.h"

const struct mh_navigation_source *
mh_navigation_mot_dat_source(void)
{
	static const struct mh_navigation_source source = {NULL,
		mh_route_mot_load, mh_camera_mot_load, mh_ai_path_dat_load};

	return &source;
}
