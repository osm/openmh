#ifndef MH_ASSETS_CAR_H
#define MH_ASSETS_CAR_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/catalog/car.h"

/*
 * Accepts decoded ASCII text (CRLF or LF). Suspension fields are an optional
 * all-or-none group; PhyTurnForce is independently optional.
 * GripLevel/PhyBrakeForce form another optional all-or-none group. Simulation
 * engine settings also form an optional all-or-none group. The runtime
 * transmission choice is supplied separately, not copied from CAR text.
 * PhyCornerFL/FR/BR/BL form the optional all-or-none body group.
 * ShadowPoints1..4 form another optional all-or-none group.
 * Colour1..3 form an optional complete paint-key group; signed 32-bit
 * integer components wrap to bytes, as in the original CAR reader.
 * Simulation stages require their settings. Output unchanged on failure.
 */
enum mh_error_code	mh_car_config_car_decode(const unsigned char *text,
	size_t size, struct mh_car *car, struct mh_error *error_out);

#endif
