#ifndef MH_BODY_POINTS_H
#define MH_BODY_POINTS_H

#include "engine/core/error.h"
#include "interfaces/catalog/car.h"

#define MH_CAR_BASE_POINTS 4
#define MH_CAR_BODY_POINTS 7

/*
 * Final suspension support count; roles 0 local, 1 remote, 2 AI. Other nonzero
 * roles follow the same four-point branch.
 */
unsigned int	mh_body_point_count(unsigned int supported_wheels,
	unsigned int role);

/*
 * First four CAR X/Z with COM Y, then three fixed upper points. Output
 * unchanged on error; no scale or COM subtraction here.
 */
enum mh_error_code	mh_car_body_points(const struct mh_car *car,
	double points[MH_CAR_BODY_POINTS][3], struct mh_error *error_out);

#endif
