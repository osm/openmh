#ifndef MH_VEHICLE_INTERNAL_H
#define MH_VEHICLE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/simulation/vehicle.h"

/*
 * Update active local cars regardless of availability. Retains motion, grip,
 * body feedback and material-stage wheel rates.
 */
enum mh_error_code	mh_vehicle_prepare_tuned(const struct mh_car *car,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out);

/*
 * Engine/forces/grip only, after mh_motion_enter. Field must complete body
 * contacts and mh_motion_leave before publishing.
 */
enum mh_error_code	mh_vehicle_road_step_tuned(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out);

#endif
