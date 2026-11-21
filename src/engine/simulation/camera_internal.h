#ifndef MH_CAMERA_INTERNAL_H
#define MH_CAMERA_INTERNAL_H

#include "engine/simulation/camera.h"

/* Private math shared by ordinary and authored track views. */
long double mh_camera_sqrt(long double value);

/* Original y+x+z order; normalization adds its separate double spill. */
long double mh_camera_length_squared(const float vector[3]);
float	mh_camera_rotate_axis(const float rotation[9], const float vector[3],
	unsigned int axis);

/* Retains degenerate prior columns; intro alone preserves vertical up. */
void	mh_camera_look_at(struct mh_camera_pose *pose, const float target[3],
	int keep_vertical_up);

#endif
