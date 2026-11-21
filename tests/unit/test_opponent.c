#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/opponent.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_car car;
	struct mh_track track;
	struct mh_opponent_config config, saved;
	double *parameters;

	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	car.has_engine = car.has_grip = car.has_suspension = car.has_steering =
		1;
	car.mass = 1000;
	car.gears = 2;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.gear_ratio[0] = 50;
	car.gear_ratio[1] = 70;
	car.gear_ratio[2] = 110;
	car.speed_level = 6;
	car.acceleration_level = 6;
	car.grip_level = 5;
	car.acceleration_force = 2800;
	car.brake_force = .4;
	track.has_ai[1] = 1;
	parameters = track.ai[1];
	parameters[0] = 8.9;
	parameters[1] = .25;
	parameters[2] = 10;
	parameters[3] = .3;
	parameters[4] = 10;
	parameters[5] = .5;
	parameters[6] = .8;
	parameters[7] = .25;
	parameters[8] = 2.7;
	parameters[9] = 16;
	CHECK(mh_opponent_configure(&car, &track, 2, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(config.speed_level == 6.5 &&
		fabs(config.acceleration_level - 7.2) < 1e-12);
	CHECK(config.vehicle.grip_level == 7.5 &&
		config.vehicle.turn_force == 14000);
	CHECK(config.vehicle.brake_force == .5 &&
		config.engine.brake_force == .5);
	CHECK(config.engine.ratio[0] == 50 &&
		fabs(config.engine.ratio[1] - 78.75) < 1e-12);
	CHECK(fabs(config.engine.acceleration - 3350) < 1e-12 &&
		config.engine.options.role_two &&
		!config.engine.options.manual);
	CHECK(config.driver.mass == 1000 && config.driver.corner_grip == 2.7 &&
		config.driver.corner_steering == 16);

	/*
	 * Original >10 opponent fallback is 5, unlike normal car clamping
	 * to 10.
	 */
	car.speed_level = car.acceleration_level = car.grip_level = 20;
	parameters[1] = parameters[3] = parameters[5] = 0;
	CHECK(mh_opponent_configure(&car, &track, 2, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(config.speed_level == 5 && config.acceleration_level == 5 &&
		config.vehicle.grip_level == 5);
	saved = config;
	CHECK(mh_opponent_configure(&car, &track, 0, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_opponent_configure(&car, &track, 4, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_opponent_configure(&car, &track, 1, &config, NULL) !=
		MH_ERROR_NONE);
	parameters[1] = 1.1;
	CHECK(mh_opponent_configure(&car, &track, 2, &config, NULL) !=
		MH_ERROR_NONE);
	parameters[1] = 0;
	parameters[8] = HUGE_VAL;
	CHECK(mh_opponent_configure(&car, &track, 2, &config, NULL) !=
		MH_ERROR_NONE);
	parameters[8] = 2;
	parameters[6] = 0;
	parameters[7] = 1;
	CHECK(mh_opponent_configure(&car, &track, 2, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &config, sizeof(config)) == 0);

	if (failures != 0)
		return 1;

	puts("Opponent fractional tuning, target truncation, fallback and atomic failures passed");

	return 0;
}
