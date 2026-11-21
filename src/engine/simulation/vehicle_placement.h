#ifndef MH_VEHICLE_PLACEMENT_H
#define MH_VEHICLE_PLACEMENT_H

#include "engine/core/error.h"
#include "engine/simulation/placement.h"
#include "engine/simulation/vehicle.h"

/*
 * Convert display origin to internal physics origin and reset all retained
 * engine, wheel, grip and body feedback. Output unchanged on failure.
 */
enum mh_error_code	mh_vehicle_place(const struct mh_engine_config *config,
	const struct mh_placement *placement, struct mh_vehicle_state *output,
	struct mh_error *error_out);

/*
 * Existing car: reset motion/history, gearbox, shake/slide amount and inverted
 * contact, retaining the other published feedback. Placement uses the same
 * display/internal conversion. State/output may alias.
 */
enum mh_error_code	mh_vehicle_relocate(
	const struct mh_engine_config *config,
	const struct mh_placement *placement,
	const struct mh_vehicle_state *state, struct mh_vehicle_state *output,
	struct mh_error *error_out);

#endif
