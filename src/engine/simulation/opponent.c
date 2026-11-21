#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/opponent.h"

#define LEVEL_LIMIT 10
#define DEFAULT_LEVEL 5
#define MASS_LIMIT 10000
#define INPUT_GRIP_LIMIT 1000
#define PARAMETER_LIMIT 100
#define CORNER_GRIP_MIN .01
#define TURN_FORCE 14000
#define ACCEL_LEVEL_STEP 250
#define RATIO_LEVEL_STEP .05
#define BASE_RATIO_FACTOR 1.05

static double
tuning_level(unsigned int base, double target, double weight)
{
	double level;

	/*
	 * The caller truncates AI target levels before passing them to setup.
	 */
	level = base + ((double)(unsigned int)target - base) * weight;

	/* Opponents fall back to five above ten, unlike player setup. */

	return level > LEVEL_LIMIT ? DEFAULT_LEVEL : level;
}

enum mh_error_code
mh_opponent_configure(const struct mh_car *car, const struct mh_track *track,
	unsigned int difficulty, struct mh_opponent_config *output,
	struct mh_error *error_out)
{
	struct mh_opponent_config result;
	struct mh_engine_options options;
	struct mh_engine_state initial_engine;
	const double *parameters;
	enum mh_error_code status;
	unsigned int parameter, gear;

	if (car == NULL || track == NULL || output == NULL || difficulty < 1 ||
		difficulty > MH_TRACK_DIFFICULTIES ||
		!track->has_ai[difficulty - 1] || !car->has_steering ||
		!car->has_suspension ||
		!(car->mass >= 1 && car->mass <= MASS_LIMIT) ||
		car->grip_level > INPUT_GRIP_LIMIT) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid opponent tuning selection");
	}

	parameters = track->ai[difficulty - 1];

	for (parameter = 0; parameter < MH_TRACK_AI_VALUES; parameter++)
		if (!(parameters[parameter] >= 0 &&
			parameters[parameter] <= PARAMETER_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid AI tuning value");

	if (parameters[MH_TRACK_AI_SPEED_WEIGHT] > 1 ||
		parameters[MH_TRACK_AI_ACCEL_WEIGHT] > 1 ||
		parameters[MH_TRACK_AI_GRIP_WEIGHT] > 1 ||
		parameters[MH_TRACK_AI_BRAKE_WEIGHT] > 1 ||
		parameters[MH_TRACK_AI_CORNER_GRIP] < CORNER_GRIP_MIN) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI tuning weight or corner grip");
	}

	memset(&result, 0, sizeof(result));
	memset(&options, 0, sizeof(options));
	options.role_two = 1;
	status = mh_engine_configure(car, &options, &result.engine, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.speed_level = tuning_level(car->speed_level,
		parameters[MH_TRACK_AI_SPEED_LEVEL],
		parameters[MH_TRACK_AI_SPEED_WEIGHT]);
	result.acceleration_level = tuning_level(car->acceleration_level,
		parameters[MH_TRACK_AI_ACCEL_LEVEL],
		parameters[MH_TRACK_AI_ACCEL_WEIGHT]);
	result.vehicle.grip_level = tuning_level(car->grip_level,
		parameters[MH_TRACK_AI_GRIP_LEVEL],
		parameters[MH_TRACK_AI_GRIP_WEIGHT]);
	result.vehicle.turn_force = TURN_FORCE;
	result.vehicle.brake_force = car->brake_force +
		(parameters[MH_TRACK_AI_BRAKE_FORCE] - car->brake_force) *
			parameters[MH_TRACK_AI_BRAKE_WEIGHT];
	result.engine.brake_force = result.vehicle.brake_force;
	result.engine.acceleration = car->acceleration_force +
		(result.acceleration_level - DEFAULT_LEVEL) * ACCEL_LEVEL_STEP;

	for (gear = 1; gear <= car->gears; gear++) {
		result.engine.ratio[gear] = car->gear_ratio[gear] *
			((result.speed_level - DEFAULT_LEVEL) *
					RATIO_LEVEL_STEP +
				BASE_RATIO_FACTOR);
	}

	status = mh_engine_reset(&result.engine, &initial_engine, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.driver.mass = car->mass;
	result.driver.corner_grip = parameters[MH_TRACK_AI_CORNER_GRIP];
	result.driver.corner_steering = parameters[MH_TRACK_AI_CORNER_STEERING];
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
