#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/forces.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-8)

static void
test_preparation_stores(void)
{
	struct mh_car car;
	struct mh_motion_state state;
	struct mh_force_input input;
	struct mh_force_result result;
	double rotation[9] = {.8, .6, 0, -.6, .8, 0, 0, 0, 1};
	static const double expected[2][6] = {
		{2935.18896484375, 0, 0, -3487.2607421875, -4649.68115234375,
			0},
		{-2203.6962890625, 0, -1450.5999755859375, -3471.16455078125,
			-4628.21923828125, 0}};

	unsigned int wheel, axis, axle;

	memset(&car, 0, sizeof(car));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	car.has_suspension = car.has_steering = 1;
	car.mass = 1450.125;
	car.turn_force = 12345.67;
	memcpy(state.rotation, rotation, sizeof(rotation));
	state.velocity[2] = 3.7;
	input.steering = .3;
	input.previous_steering = -.6;
	input.longitudinal = -725.3;
	input.phase = 13.37;
	input.wheelspin = 1;

	/* Force results, with the engine scalar supplied separately. */
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.steering == 0.30000001192092896);
	CHECK(result.smoothed_steering == -0.42000001668930054);
	CHECK(result.steering_force == 3425.923583984375);

	for (wheel = 0; wheel < 4; wheel++) {
		axle = wheel < 2 ? 0 : 1;

		for (axis = 0; axis < 3; axis++) {
			CHECK(result.forces.traction[wheel][axis] ==
				expected[axle][axis]);
			CHECK(result.forces.applied[wheel][axis] ==
				expected[axle][axis + 3]);
		}
	}
}

int
main(void)
{
	struct mh_car car;
	struct mh_motion_state state;
	struct mh_force_input input;
	struct mh_force_result result;
	struct mh_force_result saved;
	double position[3], rotation[9], total[3];
	double gravity;
	unsigned int i, j, k;

	test_preparation_stores();

	memset(&car, 0, sizeof(car));
	car.has_suspension = car.has_steering = 1;
	car.mass = 1450;
	car.turn_force = 12000;
	memset(position, 0, sizeof(position));
	memset(rotation, 0, sizeof(rotation));
	rotation[0] = rotation[4] = rotation[8] = 1;
	CHECK(mh_motion_reset(position, rotation, &state, NULL) ==
		MH_ERROR_NONE);
	memset(&input, 0, sizeof(input));
	gravity = 15.8100004196167;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.gravity, gravity);

	for (i = 0; i < 4; i++) {
		CHECK(result.forces.applied[i][1] == -5731.125);

		for (j = 0; j < 3; j++)
			NEAR(result.forces.traction[i][j], 0);
	}

	input.steering = .5;
	state.velocity[2] = 2;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, 3000);
	NEAR(result.smoothed_steering, .1);

	for (i = 0; i < 4; i++) {
		CHECK(result.forces.applied[i][1] ==
			(i < 2 ? -5774.625 : -5760.125));
		NEAR(result.forces.traction[i][0], i < 2 ? 3000 : 0);
	}

	input.previous_steering = result.smoothed_steering;
	state.velocity[2] = 4;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, 6000);
	NEAR(result.smoothed_steering, .18);
	state.velocity[2] = 40;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, 6000);

	/* Reverse steering changes only when the old sliding flag is clear. */
	state.velocity[2] = -2;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, -3000);
	input.sliding = 1;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, 3000);
	input.reduced_gravity = 1;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.gravity, gravity / 2);
	CHECK(result.forces.applied[0][1] == -2822.0625);
	input.reduced_gravity = 0;
	input.sliding = 0;
	state.velocity[2] = 4;

	/* Near-stopped AI damping can be large before clamp. */
	input.steering = 5e10;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering, 1);
	NEAR(result.steering_force, 12000);
	input.steering = -5e10;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering, -1);
	NEAR(result.steering_force, -12000);

	/*
	 * Wheelspin reshapes steering at both axles. Each rear wheel receives
	 * TWICE the engine routine's scalar, not half of it.
	 */
	input.steering = 1;
	input.longitudinal = -725;
	input.wheelspin = 1;

	for (i = 0; i < 3; i++) {
		input.phase = (i == 0 ? -1 : (i == 1 ? 0 : 1)) * acos(-1.0) /
			(2 * .033);
		CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
			MH_ERROR_NONE);
		NEAR(result.forces.traction[0][0], 12000 * (.5 + .25 * i));
		NEAR(result.forces.traction[2][0], 12000 * (-1 + .25 * i));
		NEAR(result.forces.traction[2][2], -1450);
		NEAR(result.forces.traction[3][2], -1450);
		NEAR(result.forces.traction[0][2], 0);
	}

	/* Gravity and signed speed load stay WORLD vertical on a rolled car. */
	rotation[0] = rotation[4] = 0;
	rotation[1] = 1;
	rotation[3] = -1;
	memcpy(state.rotation, rotation, sizeof(rotation));
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	memset(total, 0, sizeof(total));

	for (i = 0; i < 4; i++)
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 3; k++)
				total[j] += rotation[k * 3 + j] *
					result.forces.applied[i][k];
		}

	NEAR(total[0], 0);
	CHECK(total[1] == -23214.5);
	NEAR(total[2], 0);

	for (i = 0; i < 3; i++) {
		NEAR(result.forces.body_force[i], 0);
		NEAR(result.forces.body_torque[i], 0);
	}

	/*
	 * At rest, steering produces no force, even though the smoothed value
	 * moves.
	 */
	state.velocity[2] = 0;
	input.previous_steering = -1;
	CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.steering_force, 0);
	CHECK(result.smoothed_steering == -0.6000000238418579);

	/* Repeated smoothing uses a per-call .2 coefficient, with no dt. */
	for (i = 0; i < 20; i++) {
		input.previous_steering = result.smoothed_steering;
		CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) ==
			MH_ERROR_NONE);
	}

	/* Original 21 stored smoothing steps, not the unrounded closed form. */
	CHECK(result.smoothed_steering == 0.9815532565116882);
	saved = result;

	for (i = 0; i < 10; i++) {
		memset(&input, 0, sizeof(input));
		car.has_steering = 1;

		if (i == 0)
			input.steering = HUGE_VAL;

		if (i == 1)
			input.previous_steering = 1.01;

		if (i == 2)
			input.longitudinal = DBL_MAX;

		if (i == 3)
			input.phase = -1e10;

		if (i == 4)
			input.sliding = 2;

		if (i == 5)
			input.wheelspin = -1;

		if (i == 6)
			input.reduced_gravity = 3;

		if (i == 7)
			car.has_steering = 0;

		if (i == 8)
			state.rotation[0] = 1;

		if (i == 9)
			state.velocity[2] = HUGE_VAL;

		CHECK(mh_force_prepare(&car, &state, &input, &result, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
		state.rotation[0] = 0;
	}

	CHECK(mh_force_prepare(NULL, &state, &input, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_force_prepare(&car, NULL, &input, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_force_prepare(&car, &state, NULL, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_force_prepare(&car, &state, &input, NULL, NULL) !=
		MH_ERROR_NONE);

	if (failures != 0)
		return 1;

	puts("Gravity, speed load, steering, wheelspin split and invalid inputs passed");

	return 0;
}
