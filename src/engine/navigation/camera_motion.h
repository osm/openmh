#ifndef MH_NAVIGATION_CAMERA_MOTION_H
#define MH_NAVIGATION_CAMERA_MOTION_H

#include "interfaces/navigation/camera_motion.h"

/* Release owned decoded arrays and clear the owner. */
void	mh_camera_motion_free(struct mh_camera_motion *motion);

#endif
