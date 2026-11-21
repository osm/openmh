#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/ai_driver.h"
#include "engine/simulation/ai_traffic.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
prepare_cars(struct mh_ai_traffic_car cars[8])
{
	unsigned int i;

	memset(cars, 0, 8 * sizeof(*cars));

	for (i = 0; i < 8; i++) {
		cars[i].width = 2;
		cars[i].length = 4;
		cars[i].heading_dot = 1;
		cars[i].speed = 20;
		cars[i].point = 50;
	}
}

int
main(void)
{
	struct mh_ai_point points[128];
	struct mh_ai_section section;
	struct mh_ai_path path;
	struct mh_ai_traffic_car cars[8];
	struct mh_ai_traffic_result result, saved;
	struct mh_ai_driver_config config;
	struct mh_ai_driver_state state;
	struct mh_ai_driver_feedback feedback;
	struct mh_ai_driver_result driver, plain;
	double step;
	unsigned int i;

	memset(points, 0, sizeof(points));
	memset(&section, 0, sizeof(section));

	for (i = 0; i < 128; i++) {
		points[i].z = i;
		points[i].direction[2] = 1;
		points[i].width[0] = points[i].width[1] = 8;
	}

	path.points = points;
	path.count = 128;
	path.sections = &section;
	path.section_count = 1;
	section.count = 128;
	step = (double)(float).2;
	prepare_cars(cars);
	cars[0].lane = 2;
	CHECK(mh_ai_traffic_step(&path, cars, 1, 0, 1, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.lane == 2 - 2 * step && !result.changing &&
		!result.brake && result.threat == -1);

	/*
	 * Catch a slower car five points ahead: select the side we're already
	 * on.
	 */
	prepare_cars(cars);
	cars[0].point = 55;
	cars[0].lateral = -.5;
	cars[1].speed = 30;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.shift == step && result.changing && !result.brake &&
		result.threat == 0);
	cars[0].lateral = .5;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == -step);

	/* Exact lateral equality has no passing/braking branch. */
	cars[0].lateral = 0;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == 0 && !result.brake);

	/* Narrow road, width clearance and negative-side fallback. */
	cars[0].lateral = -.5;
	points[50].width[1] = 1;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == -step);
	points[50].width[0] = 1;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.brake && !result.changing);
	points[50].width[0] = points[50].width[1] = 8;

	/* Side blockers use strict point gap<5 and lateral gap>0. */
	cars[0].point = 54;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.blocked_left);
	cars[2].point = 49;
	cars[2].lateral = .5;
	cars[2].speed = 31;
	CHECK(mh_ai_traffic_step(&path, cars, 3, 1, 1, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.blocked_left && result.blocked_right && result.brake &&
		result.shift == 0);
	CHECK(mh_ai_traffic_step(&path, cars, 3, 1, 0, &result, NULL) ==
			MH_ERROR_NONE &&
		!result.blocked_left && result.shift == step);
	cars[2].lateral = 2 + step + (double)(float).1;
	CHECK(mh_ai_traffic_step(&path, cars, 3, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		!result.blocked_right);

	/*
	 * Strict horizon boundary; -0.1 speed delta truncates to zero, not -1.
	 */
	prepare_cars(cars);
	cars[0].point = 55;
	cars[0].lateral = -.5;
	cars[1].speed = 21;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.threat == -1);
	cars[1].speed = 21.3;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == step);
	cars[0].point = 50;
	cars[1].speed = 19.9;
	cars[1].lateral_rate = -1;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == step);
	cars[1].speed = 19.7;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 1, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == 0);

	/* Seam branch uses unsigned compare and strict inequality. */
	prepare_cars(cars);
	cars[0].point = 126;
	cars[0].speed = 22;
	cars[1].point = 2;
	cars[1].lateral = -.5;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 0, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.shift == step && !result.blocked_left);
	cars[0].speed = 20.8; /* horizon=4 (subject to representation); no
				strict seam threat. */
	cars[1].point = 8;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 0, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.threat == -1);
	cars[0].speed =
		50; /* horizon>=count: unsigned negative delta cannot pass. */
	CHECK(mh_ai_traffic_step(&path, cars, 2, 0, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.threat == -1);

	/* Invalid samples and calculations preserve the whole output. */
	saved = result;
	cars[1].heading_dot = HUGE_VAL;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 0, 1, &result, NULL) !=
		MH_ERROR_NONE);
	cars[1].heading_dot = 1;
	cars[1].point = 128;
	CHECK(mh_ai_traffic_step(&path, cars, 2, 0, 1, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_ai_traffic_step(&path, cars, 9, 0, 1, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);

	/*
	 * Traffic brakes override pedals but leave the recovery command alone;
	 * changing doubles all normal steering terms before the heading
	 * override.
	 */
	config.mass = 1000;
	config.corner_grip = 2;
	config.corner_steering = 14;
	memset(&feedback, 0, sizeof(feedback));
	feedback.z = 50;
	feedback.x = 1;
	feedback.heading_z = 1;
	feedback.speed = 20;
	CHECK(mh_ai_driver_start(&path, 1, 50, &state, NULL) == MH_ERROR_NONE);
	memset(&result, 0, sizeof(result));
	result.lane = 1;
	result.changing = result.brake = 1;
	CHECK(mh_ai_driver_step_traffic(&path, &config, &state, &feedback, .1,
		&result, &driver, NULL) == MH_ERROR_NONE);
	result.changing = 0;
	CHECK(mh_ai_driver_step_traffic(&path, &config, &state, &feedback, .1,
		&result, &plain, NULL) == MH_ERROR_NONE);
	CHECK(driver.throttle == 0 && driver.brake == 1 &&
		driver.steering == 2 * plain.steering);
	feedback.heading_x = 1;
	feedback.heading_z = 0;
	result.changing = 1;
	CHECK(mh_ai_driver_step_traffic(&path, &config, &state, &feedback, .1,
		&result, &driver, NULL) == MH_ERROR_NONE &&
		driver.steering == -1);
	feedback.heading_x = 0;
	feedback.heading_z = 1;
	feedback.speed = 0;
	CHECK(mh_ai_driver_step_traffic(&path, &config, &state, &feedback, .1,
		&result, &driver, NULL) == MH_ERROR_NONE);
	CHECK(driver.state.stuck_seconds == .1 && driver.brake == 1 &&
		driver.throttle == 0);
	feedback.speed = 1e-9;
	state.previous_lateral = 0;
	CHECK(mh_ai_driver_step_traffic(&path, &config, &state, &feedback, .1,
		&result, &driver, NULL) == MH_ERROR_NONE);
	CHECK(driver.steering < -1e9);

	return failures != 0;
}
