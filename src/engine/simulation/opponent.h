#ifndef MH_OPPONENT_H
#define MH_OPPONENT_H

#include "engine/core/error.h"
#include "engine/simulation/ai_driver.h"
#include "engine/simulation/vehicle.h"
#include "interfaces/catalog/track.h"

struct mh_opponent_config {
	struct mh_engine_config		engine;
	struct mh_vehicle_tuning	vehicle;
	struct mh_ai_driver_config	driver;
	double				speed_level, acceleration_level;
};

/*
 * Opponent setup, using selected AI1/2/3 values. Difficulty 1..3. Automatic
 * role-two engine, fractional tuning, fixed 14000 steering force. Initial setup
 * only; field_tuning applies later launch/catch-up adjustments. C89, no
 * allocation or globals; output unchanged on failure.
 */
enum mh_error_code	mh_opponent_configure(const struct mh_car *car,
	const struct mh_track *track, unsigned int difficulty,
	struct mh_opponent_config *output, struct mh_error *error_out);

#endif
