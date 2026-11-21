#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/engine.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-8)

struct engine_store_case {
	int		manual, role_two;
	unsigned int	gear;
	double		rpm, throttle, brake, steering, speed, rear_limit;
	int		rear_supported, handbrake;
	double		dt, output_rpm, force, braking, output_speed;
};

static void
test_stored_wheelspin_threshold(void)
{
	struct mh_engine_config config;
	struct mh_engine_state state;
	struct mh_engine_input input;
	struct mh_engine_feedback feedback;
	struct mh_engine_result result;
	unsigned int rate;

	memset(&config, 0, sizeof(config));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&feedback, 0, sizeof(feedback));
	config.gears = 2;
	config.ratio[0] = 50;
	config.ratio[1] = 77;
	config.ratio[2] = 140;
	config.min_rpm = 1000;
	config.max_rpm = 10000;
	config.acceleration = 3050;
	config.brake_force = .5;
	state.gear = 1;
	state.up_ready = state.down_ready = 1;
	input.throttle = 1;
	feedback.rear_supported = 1;

	/*
	 * At equality, the stored float must select ordinary grip. Keeping the
	 * double estimate incorrectly selects wheelspin.
	 */
	for (rate = 30; rate <= 240; rate *= 2) {
		state.rpm = 7229.166015625;
		input.steering = .48653340339660645;
		feedback.forward_velocity = 5.843873023986816;
		feedback.rear_limit = 4650.28173828125;
		CHECK(mh_engine_step(&config, &state, &input, &feedback,
			1.0 / rate, &result, NULL) == MH_ERROR_NONE);
		CHECK(result.spin_estimate == 4650.28173828125);
		CHECK(!result.spin_eligible && !result.wheelspin);
		feedback.rear_limit = 4650.28125;
		CHECK(mh_engine_step(&config, &state, &input, &feedback,
			1.0 / rate, &result, NULL) == MH_ERROR_NONE);
		CHECK(result.spin_eligible && result.wheelspin);

		/* The earlier float store is independently observable. */
		state.rpm = 1521.9903564453125;
		input.steering = .5074357390403748;
		feedback.forward_velocity = 0;
		feedback.rear_limit = 994.9522705078125;
		CHECK(mh_engine_step(&config, &state, &input, &feedback,
			1.0 / rate, &result, NULL) == MH_ERROR_NONE);
		CHECK(result.spin_estimate == 994.9523315429688);
		CHECK(result.spin_eligible && result.wheelspin);

		/* Wrong eligibility previously also caused an early upshift. */
		state.rpm = 11334.873046875;
		input.steering = .8566535711288452;
		feedback.forward_velocity = 11.437471389770508;
		feedback.rear_limit = 6240.46630859375;
		CHECK(mh_engine_step(&config, &state, &input, &feedback,
			1.0 / rate, &result, NULL) == MH_ERROR_NONE);
		CHECK(!result.spin_eligible && result.state.gear == 1);
	}
}

static void
test_original_engine_stores(void)
{
	static const struct engine_store_case cases[] = {
		{0, 0, 1, 2345.67, .1234, .2345, .13, 12.345, 9999, 1, 0, .04,
			6194.51953125, 305.13909912109375, .1172500029206276,
			44.44200134277344},
		{0, 0, 1, 4567.89, .8765, .12, .789, 3.456, 2000, 1, 0,
			1.0 / 120, 4572.56005859375, 888.1283569335938,
			.05999999865889549, 12.441600799560547},
		{0, 1, 2, 7654.32, .8765, .12, -.456, 98.765, 9999, 1, 0,
			1.0 / 60, 23857.041015625, -5214.0859375,
			.05999999865889549, 355.5539855957031},
		{0, 0, 1, 3456.78, .8765, .12, .13, 12.345, 9999, 0, 0,
			1.0 / 120, 3631.043212890625, 4980.29052734375,
			.05999999865889549, 44.44200134277344},
		{0, 0, 0, 4567.89, .123, .987, .123, -3.456, 9999, 0, 1, .04,
			4327.89013671875, -3729.15234375, .31150001287460327,
			12.441600799560547},
		{1, 0, 2, 8765.43, .123, .2345, .123, -234.56, 9999, 1, 0,
			1.0 / 120, 55283.88671875, 1702.5858154296875,
			.1172500029206276, 844.416015625},
		{1, 0, 2, 8765.43, .123, .2345, .123, .3456, 9999, 1, 0,
			1.0 / 120, 1079.981689453125, -460.42144775390625,
			.1172500029206276, 1.2441600561141968},
		{1, 0, 0, 4567.89, .8765, .2345, .123, -.3456, 9999, 1, 0,
			1.0 / 120, 1223.9488525390625, -3020.498291015625,
			.1172500029206276, 1.2441600561141968}};

	struct mh_engine_config config;
	struct mh_engine_state state;
	struct mh_engine_input input;
	struct mh_engine_feedback feedback;
	struct mh_engine_result result;
	unsigned int i;
	double negative_zero = -0.0;

	memset(&config, 0, sizeof(config));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&feedback, 0, sizeof(feedback));
	config.gears = 2;
	config.ratio[0] = 50;
	config.ratio[1] = 77;
	config.ratio[2] = 140;
	config.min_rpm = 1000;
	config.max_rpm = 10000;
	config.acceleration = 3050;
	config.brake_force = .5;
	state.up_ready = state.down_ready = 1;

	/*
	 * Exact stored outputs with binary53 intermediates, including
	 * ordinary/role-two/spin curves, both free-rev paths and manual drag.
	 * Decimal inputs also exercise binary32 field reads.
	 */
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		config.options.manual = cases[i].manual;
		config.options.role_two = cases[i].role_two;
		state.gear = cases[i].gear;
		state.rpm = cases[i].rpm;
		input.throttle = cases[i].throttle;
		input.brake = cases[i].brake;
		input.steering = cases[i].steering;
		input.handbrake = cases[i].handbrake;
		feedback.forward_velocity = cases[i].speed;
		feedback.rear_limit = cases[i].rear_limit;
		feedback.rear_supported = cases[i].rear_supported;
		CHECK(mh_engine_step(&config, &state, &input, &feedback,
			cases[i].dt, &result, NULL) == MH_ERROR_NONE);
		CHECK(result.state.rpm == cases[i].output_rpm);
		CHECK(result.longitudinal == cases[i].force);
		CHECK(result.braking == cases[i].braking);
		CHECK(result.speed == cases[i].output_speed);
	}

	/*
	 * A captured halfway product distinguishes 53- from 64-bit precision.
	 */
	config.options.manual = config.options.role_two = 0;
	state.gear = 1;
	state.rpm = 9455.5322265625;
	input.throttle = 1;
	input.brake = 0;
	input.steering = -1;
	input.handbrake = 0;
	feedback.forward_velocity = 2.279794931411743;
	feedback.rear_limit = 8509.978515625;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.spin_estimate == 8509.978515625);
	CHECK(!result.spin_eligible);

	/* The original speed branch preserves a negative-zero input. */
	feedback.forward_velocity = negative_zero;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&result.speed, &negative_zero, sizeof(negative_zero)) ==
		0);
}

int
main(void)
{
	struct mh_car car;
	struct mh_engine_options options;
	struct mh_engine_config config;
	struct mh_engine_config saved_config;
	struct mh_engine_state state;
	struct mh_engine_input input;
	struct mh_engine_feedback feedback;
	struct mh_engine_result result;
	struct mh_engine_result saved;
	unsigned int i;

	memset(&car, 0, sizeof(car));
	memset(&options, 0, sizeof(options));
	memset(&input, 0, sizeof(input));
	memset(&feedback, 0, sizeof(feedback));
	car.has_engine = car.has_grip = 1;
	car.gears = 6;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.acceleration_force = 2800;
	car.brake_force = .5;
	car.acceleration_level = car.speed_level = 6;
	car.gear_ratio[0] = 50;

	for (i = 1; i <= 6; i++)
		car.gear_ratio[i] = 70 * i;

	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	NEAR(config.acceleration, 3050);
	NEAR(config.ratio[0], 50);
	NEAR(config.ratio[1], 77);
	options.manual = options.double_acceleration =
		options.reduced_acceleration = 1;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	NEAR(config.acceleration, 5060);
	car.acceleration_level = 0;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	NEAR(config.acceleration, 3960);
	car.acceleration_level = car.speed_level = 1000;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	NEAR(config.acceleration, 8910);
	NEAR(config.ratio[1], 91);
	saved_config = config;
	car.gears = 7;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&config, &saved_config, sizeof(config)) == 0);
	car.gears = 6;
	car.acceleration_level = car.speed_level = 6;
	memset(&options, 0, sizeof(options));
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_engine_reset(&config, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.gear == 1 && state.rpm == 1000 && state.up_ready &&
		state.down_ready && !state.detached);

	/*
	 * With no previous rear contact the first tick spins, but its RPM is
	 * free revving; next tick uses OLD detached to suppress wheelspin.
	 */
	input.throttle = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.spin_eligible && result.wheelspin &&
		result.state.detached);
	NEAR(result.longitudinal, 3000);
	NEAR(result.state.rpm, 2200);
	feedback.rear_supported = 1;
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE);
	CHECK(result.spin_eligible && !result.wheelspin &&
		!result.state.detached);
	input.steering = 5e10;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.spin_estimate > 1e12 && result.wheelspin);
	input.steering = 0;
	feedback.rear_limit = 1e6;

	/*
	 * Auto shift thresholds use wheel-derived RPM; reaching an upshift
	 * preserves that old-gear RPM for the current output.
	 */
	feedback.forward_velocity = 20;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.gear == 2 && result.state.rpm > 9120);
	state.gear = 2;
	feedback.forward_velocity = 2;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE &&
		result.state.gear == 1);

	/*
	 * Full brake first prevents a stationary forward->reverse transition;
	 * partial brake engages reverse, then that pedal becomes propulsion.
	 */
	state.gear = 1;
	feedback.forward_velocity = 0;
	input.throttle = 0;
	input.brake = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.gear == 1 && result.heavy_brake &&
		result.brake_active);
	input.brake = .5;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE &&
		result.state.gear == 0);
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE);
	CHECK(result.longitudinal < 0 && !result.brake_active &&
		result.braking == 0);
	input.throttle = 1;
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE);
	CHECK(result.brake == 0 && result.heavy_brake &&
		result.state.gear == 1);

	/* Handbrake ADDS half the coefficient below the threshold. */
	input.throttle = 0;
	input.brake = .2;
	input.handbrake = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.braking, .35);
	input.brake = .5;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.braking, .25);

	/* Manual shifts happen once on release, including simultaneous keys. */
	config.options.manual = 1;
	memset(&input, 0, sizeof(input));
	input.throttle = 1;
	input.shift_up = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.gear == 1 && !result.state.up_ready &&
		result.state.detached && result.longitudinal == 0);
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE &&
		result.state.gear == 1);
	input.shift_up = 0;
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE);
	CHECK(result.state.gear == 2 && result.state.up_ready &&
		!result.state.detached && result.candidate_gear == 2);
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE &&
		result.state.gear == 2);
	input.shift_up = input.shift_down = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	input.shift_up = input.shift_down = 0;
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) == MH_ERROR_NONE &&
		result.state.gear == 1);

	/*
	 * Manual downshift into reverse is blocked at +5 world units exactly.
	 */
	state.down_ready = 0;
	feedback.forward_velocity = 5;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE &&
		result.state.gear == 1);
	feedback.forward_velocity = 4.999;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE &&
		result.state.gear == 0);
	CHECK(result.candidate_gear == 0 && result.longitudinal < 0);

	/*
	 * Free revving deliberately exceeds max RPM; airborne auto reverse
	 * instead decays to idle because its signed drive is negative.
	 */
	state.gear = 1;
	state.down_ready = 1;
	state.rpm = 10000;
	feedback.forced_detach = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.state.rpm, 10300);
	config.options.manual = 0;
	state.gear = 0;
	input.throttle = 0;
	input.brake = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.state.rpm, 9760);

	/*
	 * Control clamps, role-two suppression, and failures including a
	 * negative sqrt input leave the caller's aliased result untouched.
	 */
	state.gear = 1;
	state.rpm = 1000;
	input.throttle = 2;
	input.brake = -1;
	feedback.forward_velocity = 0;
	feedback.rear_limit = 0;
	config.options.role_two = config.options.suppress_role_two_spin = 1;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 1 && result.brake == 0 &&
		!result.spin_eligible);
	saved = result;
	input.throttle = DBL_MAX;
	CHECK(mh_engine_step(&config, &result.state, &input, &feedback, .04,
		&result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	input.throttle = 1;
	config.options.suppress_role_two_spin = 0;
	state.rpm = 500;
	CHECK(mh_engine_step(&config, &state, &input, &feedback, .04, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);

	test_stored_wheelspin_threshold();
	test_original_engine_stores();

	if (failures)
		return 1;

	puts("Engine tuning, contact feedback, automatic/manual shifts, reverse and failure atomicity passed");

	return 0;
}
