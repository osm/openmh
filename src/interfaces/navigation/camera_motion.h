#ifndef MH_NAVIGATION_CAMERA_MOTION_DATA_H
#define MH_NAVIGATION_CAMERA_MOTION_DATA_H

#include <stddef.h>

/*
 * Camera records retain binary32 values without animation-key rules. Consumers
 * use these fields directly.
 */
struct mh_camera_key {
	float	frame;
	float	selector;
	float	focal;
	float	far_factor;
	float	distance_cap;
	float	anchor[3];
	float	parameter[6];
};

struct mh_camera_motion {
	size_t			count;
	struct mh_camera_key	*keys;
};

#endif
