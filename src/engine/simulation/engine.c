#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/engine.h"

/* Native validity bounds; they do not replace original clamps. */
#define INPUT_LEVEL_LIMIT 1000
#define INPUT_ACCELERATION_LIMIT 1e6
#define INPUT_RATIO_LIMIT 10000
#define RATIO_LIMIT 20000
#define RPM_LIMIT 1e6
#define MIN_MAX_RPM 1000
#define ACCELERATION_LIMIT 3e6
#define MIN_BRAKE_FORCE 1e-6
#define BRAKE_FORCE_LIMIT 1000
#define PEDAL_LIMIT 1e4
#define STEERING_LIMIT 1e12
#define VELOCITY_LIMIT 1e4
#define REAR_LIMIT_MAX 1e6
#define STEP_LIMIT .1
#define FORCE_LIMIT 1e8
#define SPIN_LIMIT 1e24

#define LEVEL_LIMIT 10
#define BASE_LEVEL 5
#define REDUCED_LEVEL_MIN 4
#define LEVEL_REDUCTION 3
#define RATIO_LEVEL_STEP .05
#define BASE_RATIO_FACTOR 1.05
#define ACCEL_LEVEL_STEP 250
#define MANUAL_ACCEL_FACTOR 1.1
#define KMH_PER_MPS 3.6
#define FIRST_GEAR_STEER_SPIN 500
#define RPM_SCALE .001
#define SPIN_SPEED_START .3
#define SPIN_SPEED_SPAN .7
#define SPIN_THROTTLE_MIN .1
#define BRAKE_ACTIVE_LEVEL .15
#define HEAVY_BRAKE_LEVEL .9
#define HANDBRAKE_FORCE_SHARE .5
#define COAST_DRIVE_LIMIT .15
#define ENGINE_BRAKE_RPM_FACTOR .7
#define ENGINE_DRAG 13
#define SPIN_GAIN .02
#define SPIN_IDLE_PUSH 80
#define SPIN_REV_RATE 27.5
#define SPIN_RPM_DECAY .625
#define SPIN_CURVE_SCALE .00097
#define ROLE_TWO_CURVE_CEILING .00093
#define ROLE_TWO_CURVE_SCALE .00097
#define DEFAULT_CURVE_CEILING .000935
#define GEAR_CEILING_STEP .15
#define SPIN_FORCE_SCALE .0005
#define SPIN_FORCE_DAMPING .7
#define LAUNCH_SPEED 10
#define LAUNCH_FORCE 300
#define DIRECTION_SPEED 2
#define DOWNSHIFT_RPM_FACTOR .6
#define UPSHIFT_RPM_FACTOR .912
#define REVERSE_BRAKE_MIN .05
#define DIRECTION_PEDAL_LEVEL .25
#define REVERSE_SHIFT_SPEED 5
#define FREE_REV_DRIVE_MIN .1
#define FREE_REV_RPM_BOOST 3000
#define FREE_REV_GAIN .1
#define FREE_REV_MIN_INCREMENT 10
#define FREE_REV_RATE 25
#define FREE_REV_DECAY 6000

/*
 * Original runtime inputs are binary32, even though the native API is double.
 */
struct stored_engine_inputs {
	struct mh_engine_config		config;
	struct mh_engine_state		state;
	struct mh_engine_input		controls;
	struct mh_engine_feedback	feedback;
};

/* RPM before the gearbox/free-rev tail, and the two force-curve operands. */
struct engine_curve {
	double	rpm, rpm_thousands, denominator;
};

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

static int
bounded(double value, double limit)
{
	return value >= -limit && value <= limit;
}

static double
clamp(double value, double low, double high)
{
	if (value < low)
		return low;

	return value > high ? high : value;
}

static int
valid_options(const struct mh_engine_options *options)
{
	return valid_flag(options->manual) &&
		valid_flag(options->reduced_acceleration) &&
		valid_flag(options->double_acceleration) &&
		valid_flag(options->role_two) &&
		valid_flag(options->suppress_role_two_spin);
}

static int
valid_config(const struct mh_engine_config *config)
{
	unsigned int i;

	if (config == NULL || config->gears < 1 ||
		config->gears > MH_CAR_MAX_GEARS ||
		!(config->min_rpm >= 1 && config->min_rpm < config->max_rpm &&
			config->max_rpm >= MIN_MAX_RPM &&
			config->max_rpm <= RPM_LIMIT) ||
		!bounded(config->acceleration, ACCELERATION_LIMIT) ||
		!(config->brake_force >= MIN_BRAKE_FORCE &&
			config->brake_force <= BRAKE_FORCE_LIMIT) ||
		!valid_options(&config->options))
		return 0;

	for (i = 0; i <= config->gears; i++)
		if (!(config->ratio[i] >= 1 && config->ratio[i] <= RATIO_LIMIT))
			return 0;

	return 1;
}

enum mh_error_code
mh_engine_configure(const struct mh_car *car,
	const struct mh_engine_options *options,
	struct mh_engine_config *output, struct mh_error *error_out)
{
	struct mh_engine_config result;
	unsigned int level;
	unsigned int i;
	double scale;

	if (car == NULL || options == NULL || output == NULL ||
		!car->has_engine || !car->has_grip || car->gears < 1 ||
		car->gears > MH_CAR_MAX_GEARS ||
		car->acceleration_level > INPUT_LEVEL_LIMIT ||
		car->speed_level > INPUT_LEVEL_LIMIT ||
		!valid_options(options) ||
		!(car->acceleration_force >= 0 &&
			car->acceleration_force <= INPUT_ACCELERATION_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid engine configuration input");

	memset(&result, 0, sizeof(result));
	result.options = *options;
	result.gears = car->gears;
	result.min_rpm = car->min_rpm;
	result.max_rpm = car->max_rpm;
	result.brake_force = car->brake_force;
	level = car->speed_level > LEVEL_LIMIT ? LEVEL_LIMIT : car->speed_level;
	scale = ((double)level - BASE_LEVEL) * RATIO_LEVEL_STEP +
		BASE_RATIO_FACTOR;

	for (i = 0; i <= car->gears; i++) {
		if (!(car->gear_ratio[i] >= 1 &&
			car->gear_ratio[i] <= INPUT_RATIO_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car gear ratio");

		result.ratio[i] = car->gear_ratio[i] * (i == 0 ? 1 : scale);
	}

	level = car->acceleration_level;

	if (options->reduced_acceleration)
		level = (level < REDUCED_LEVEL_MIN ? REDUCED_LEVEL_MIN
						: level) -
			LEVEL_REDUCTION;

	if (level > LEVEL_LIMIT)
		level = LEVEL_LIMIT;

	result.acceleration = car->acceleration_force +
		((double)level - BASE_LEVEL) * ACCEL_LEVEL_STEP;

	if (options->double_acceleration)
		result.acceleration *= 2;

	if (options->manual)
		result.acceleration *= MANUAL_ACCEL_FACTOR;

	if (!valid_config(&result))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"engine tuning exceeds native bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_engine_reset(const struct mh_engine_config *config,
	struct mh_engine_state *output, struct mh_error *error_out)
{
	struct mh_engine_state state;

	if (!valid_config(config) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid engine reset arguments");

	memset(&state, 0, sizeof(state));
	state.gear = 1;
	state.rpm = config->min_rpm;
	state.up_ready = state.down_ready = 1;
	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
shift_automatic(const struct mh_engine_config *config, double forward_velocity,
	double raw_rpm, struct mh_engine_result *result)
{
	/* Shifts use this tick's raw RPM with the OLD ratio. */
	if (forward_velocity >= DIRECTION_SPEED) {
		if (result->state.gear == 0)
			result->state.gear = 1;
		else if (result->state.gear > 1 &&
			raw_rpm < config->max_rpm * DOWNSHIFT_RPM_FACTOR)
			result->state.gear--;

		if (raw_rpm > config->max_rpm * UPSHIFT_RPM_FACTOR &&
			result->state.gear < config->gears)
			result->state.gear++;
	}

	if (forward_velocity <= -DIRECTION_SPEED &&
		result->brake > REVERSE_BRAKE_MIN)
		result->state.gear = 0;

	if (fabs(forward_velocity) < DIRECTION_SPEED &&
		(!result->heavy_brake || result->state.gear == 0)) {
		if (result->brake > DIRECTION_PEDAL_LEVEL &&
			result->throttle < DIRECTION_PEDAL_LEVEL)
			result->state.gear = 0;

		if (result->brake < DIRECTION_PEDAL_LEVEL &&
			result->throttle > DIRECTION_PEDAL_LEVEL)
			result->state.gear = 1;
	}
}

static void
apply_manual_braking(const struct mh_engine_config *config,
	const struct mh_engine_state *state, double ratio,
	double forward_velocity, struct mh_engine_result *result)
{
	double engine_drag;
	volatile double value;

	if (result->throttle < COAST_DRIVE_LIMIT && !state->detached) {
		engine_drag = ENGINE_DRAG;

		if (fabs(forward_velocity) > ratio) {
			value = fabs(forward_velocity) / ratio;
			engine_drag = (float)(value * ENGINE_DRAG);
		}

		value = engine_drag * state->rpm;
		value /= ratio;
		value = forward_velocity > 0 ? result->longitudinal - value
					: result->longitudinal + value;
		result->longitudinal = (float)value;
		result->engine_braking =
			state->rpm > config->max_rpm * ENGINE_BRAKE_RPM_FACTOR;
	}

	if (forward_velocity != 0) {
		value = clamp(forward_velocity, -1, 1) * result->brake;
		value *= config->brake_force;
		result->longitudinal = (float)(result->longitudinal - value);
	}
}

static void
release_manual_shifts(const struct mh_engine_config *config,
	const struct mh_engine_input *input, double forward_velocity,
	struct mh_engine_result *result)
{
	/* Each release consumes its latch exactly once. */
	if (!input->shift_down && !result->state.down_ready) {
		result->state.down_ready = 1;

		if (result->state.gear == 1 &&
			forward_velocity < REVERSE_SHIFT_SPEED)
			result->state.gear--;

		if (result->state.gear > 1)
			result->state.gear--;
	}

	if (!input->shift_up && !result->state.up_ready) {
		if (result->state.gear < config->gears)
			result->state.gear++;

		result->state.up_ready = 1;
	}
}

static void
apply_manual_controls(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input, double ratio,
	double forward_velocity, unsigned int candidate_gear,
	struct mh_engine_result *result)
{
	/* Held shifts cut propulsion before braking terms. */
	if (input->shift_down) {
		result->state.down_ready = 0;
		result->longitudinal = 0;
	}

	if (input->shift_up) {
		result->state.up_ready = 0;
		result->longitudinal = 0;
	}

	if (candidate_gear == 0)
		result->longitudinal = -result->longitudinal;

	apply_manual_braking(config, state, ratio, forward_velocity, result);
	release_manual_shifts(config, input, forward_velocity, result);
}

static void
update_rpm_and_detachment(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_feedback *feedback, double drive_command,
	double raw_rpm, double step_seconds, struct mh_engine_result *result)
{
	double rpm_increment;
	volatile double value;

	/* Rear feedback is from the previous tick's final pass. */
	result->state.detached = !result->state.up_ready ||
		!result->state.down_ready || !feedback->rear_supported ||
		feedback->forced_detach;

	if (!result->state.detached)
		result->state.rpm = raw_rpm;
	else if (drive_command > FREE_REV_DRIVE_MIN) {
		value = config->max_rpm - state->rpm;
		value += FREE_REV_RPM_BOOST;
		value *= FREE_REV_GAIN;
		value *= drive_command;
		rpm_increment = (float)value;

		if (rpm_increment < FREE_REV_MIN_INCREMENT)
			rpm_increment = FREE_REV_MIN_INCREMENT;

		value = step_seconds * FREE_REV_RATE;
		value *= rpm_increment;
		result->state.rpm = (float)(value + state->rpm);
	} else {
		value = step_seconds * FREE_REV_DECAY;
		result->state.rpm = (float)(state->rpm - value);

		if (result->state.rpm < config->min_rpm)
			result->state.rpm = config->min_rpm;
	}
}

static enum mh_error_code
validate_step(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input,
	const struct mh_engine_feedback *feedback, double step_seconds,
	struct mh_engine_result *output, struct mh_error *error_out)
{
	if (!valid_config(config) || state == NULL || input == NULL ||
		feedback == NULL || output == NULL ||
		state->gear > config->gears ||
		!(state->rpm >= 0 && state->rpm <= RPM_LIMIT) ||
		!valid_flag(state->up_ready) ||
		!valid_flag(state->down_ready) ||
		!valid_flag(state->detached) ||
		!bounded(input->throttle, PEDAL_LIMIT) ||
		!bounded(input->brake, PEDAL_LIMIT) ||
		!bounded(input->steering, STEERING_LIMIT) ||
		!valid_flag(input->shift_up) ||
		!valid_flag(input->shift_down) ||
		!valid_flag(input->handbrake) ||
		!bounded(feedback->forward_velocity, VELOCITY_LIMIT) ||
		!(feedback->rear_limit >= 0 &&
			feedback->rear_limit <= REAR_LIMIT_MAX) ||
		!valid_flag(feedback->rear_supported) ||
		!valid_flag(feedback->forced_detach) ||
		!(step_seconds > 0 && step_seconds <= STEP_LIMIT)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid engine state, controls, feedback or timestep");
	}

	return MH_ERROR_NONE;
}

static void
store_engine_inputs(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input,
	const struct mh_engine_feedback *feedback,
	struct stored_engine_inputs *stored)
{
	/* Read binary32 runtime/configuration and control fields. */
	stored->config = *config;

	stored->config.min_rpm = (float)config->min_rpm;
	stored->config.max_rpm = (float)config->max_rpm;
	stored->config.acceleration = (float)config->acceleration;
	stored->config.brake_force = (float)config->brake_force;
	stored->state = *state;
	stored->state.rpm = (float)state->rpm;
	stored->controls = *input;
	stored->controls.throttle = (float)input->throttle;
	stored->controls.brake = (float)input->brake;
	stored->controls.steering = (float)input->steering;
	stored->feedback = *feedback;
	stored->feedback.forward_velocity = (float)feedback->forward_velocity;
	stored->feedback.rear_limit = (float)feedback->rear_limit;
}

static unsigned int
pending_gear(const struct mh_engine_state *state, unsigned int gears)
{
	unsigned int candidate_gear;

	/* Pending shifts affect candidate, not the ratio lookup. */
	candidate_gear = state->gear;

	if (!state->up_ready && candidate_gear < gears)
		candidate_gear++;

	if (!state->down_ready) {
		if (state->gear > 0)
			candidate_gear--;
		else
			candidate_gear = 1;
	}

	return candidate_gear;
}

static void
evaluate_wheelspin(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input,
	const struct mh_engine_feedback *feedback, double ratio, double speed,
	struct mh_engine_result *result)
{
	static const double spin_base[MH_CAR_MAX_GEARS + 1] = {
		0, 400, 350, 330, 0, 0, 0};

	volatile double spin, value;

	spin = spin_base[result->candidate_gear];

	if (state->gear == 1) {
		value = fabs(input->steering) * FIRST_GEAR_STEER_SPIN;
		spin = (float)(spin + value);
	}

	/*
	 * Use binary53 operations, with binary32 stores before testing
	 * eligibility. The speed used here is still binary64; the later
	 * displayed speed is a float.
	 */
	value = ratio * SPIN_SPEED_START;

	if (value > speed)
		value = state->rpm * RPM_SCALE;
	else {
		value = ratio * SPIN_SPEED_SPAN;
		value = (ratio - speed) / value;
		value *= state->rpm * RPM_SCALE;
	}

	spin *= value;
	result->spin_estimate = (float)spin;
	result->spin_eligible = result->spin_estimate > feedback->rear_limit &&
		result->throttle > SPIN_THROTTLE_MIN &&
		result->candidate_gear > 0;

	if (config->options.role_two && config->options.suppress_role_two_spin)
		result->spin_eligible = 0;

	result->wheelspin = result->spin_eligible && !state->detached;
}

static double
prepare_pedals(const struct mh_engine_config *config,
	const struct mh_engine_state *state, struct mh_engine_result *result)
{
	double brake_command, drive_command;

	/* Eligibility uses the pedals BEFORE heavy braking suppresses drive. */
	brake_command = !config->options.manual && state->gear == 0
		? result->throttle
		: result->brake;
	result->brake_active = brake_command > BRAKE_ACTIVE_LEVEL;
	result->heavy_brake = brake_command > HEAVY_BRAKE_LEVEL;

	if (result->heavy_brake) {
		if (!config->options.manual && state->gear == 0)
			result->brake = 0;
		else
			result->throttle = 0;
	}

	drive_command = !config->options.manual && state->gear == 0
		? -result->brake
		: result->throttle;
	result->braking = (float)(brake_command * config->brake_force);

	return drive_command;
}

static void
apply_automatic_engine_drag(const struct mh_engine_config *config,
	const struct mh_engine_state *state, double ratio,
	double forward_velocity, double drive_command,
	struct mh_engine_result *result)
{
	double engine_drag;
	volatile double value;

	if (!config->options.manual &&
		fabs(drive_command) < COAST_DRIVE_LIMIT && !state->detached) {
		result->engine_braking =
			state->rpm > config->max_rpm * ENGINE_BRAKE_RPM_FACTOR;
		value = state->rpm * ENGINE_DRAG;
		engine_drag = value / ratio;
		result->longitudinal = (float)(forward_velocity > 0
				? result->longitudinal - engine_drag
				: result->longitudinal + engine_drag);
	}
}

static void
prepare_wheelspin_curve(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_feedback *feedback, double drive_command,
	double step_seconds, const struct mh_engine_result *result,
	struct engine_curve *curve)
{
	volatile double value;

	value = result->spin_estimate - feedback->rear_limit;
	value *= SPIN_GAIN;
	value += SPIN_IDLE_PUSH;
	value *= drive_command;
	value *= step_seconds * SPIN_REV_RATE;
	curve->rpm = (float)(value + state->rpm);
	value = pow(SPIN_RPM_DECAY, step_seconds);
	curve->rpm = (float)(value * curve->rpm);

	if (curve->rpm > config->max_rpm)
		curve->rpm = config->max_rpm;

	curve->rpm_thousands =
		(float)((state->rpm - config->min_rpm) * RPM_SCALE);
	curve->denominator =
		config->max_rpm * SPIN_CURVE_SCALE - curve->rpm_thousands;
}

static void
prepare_road_speed_curve(const struct mh_engine_config *config, double ratio,
	const struct mh_engine_result *result, struct engine_curve *curve)
{
	double curve_ceiling;
	volatile double value;

	value = config->max_rpm - config->min_rpm;
	value *= result->speed;
	value /= ratio;
	curve->rpm = (float)(value + config->min_rpm);

	if (curve->rpm < config->min_rpm + 1)
		curve->rpm = (float)(config->min_rpm + 1);

	curve->rpm_thousands =
		(float)((curve->rpm - config->min_rpm) * RPM_SCALE);

	if (!config->options.manual && config->options.role_two) {
		curve_ceiling = config->max_rpm * ROLE_TWO_CURVE_CEILING;
		curve->denominator = config->max_rpm * ROLE_TWO_CURVE_SCALE;
	} else {
		curve_ceiling = config->max_rpm * DEFAULT_CURVE_CEILING -
			result->candidate_gear * GEAR_CEILING_STEP;
		curve->denominator = config->max_rpm * RPM_SCALE;
	}

	if (curve->rpm_thousands > curve_ceiling)
		curve->rpm_thousands = (float)curve_ceiling;

	curve->denominator -= curve->rpm_thousands;
}

static enum mh_error_code
apply_drive_force(const struct mh_engine_config *config,
	const struct mh_engine_feedback *feedback,
	const struct engine_curve *curve, double drive_command,
	struct mh_engine_result *result, struct mh_error *error_out)
{
	double force_curve;
	volatile double value;

	if (!(curve->rpm_thousands >= 0) || curve->denominator == 0)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"engine curve outside real finite domain");

	value = 2 / curve->denominator;
	force_curve = sqrt(curve->rpm_thousands) - value;

	if (result->spin_eligible) {
		value = result->spin_estimate * SPIN_FORCE_SCALE;
		value *= result->spin_estimate;
		value = feedback->rear_limit / value;
		value *= SPIN_FORCE_DAMPING;
		force_curve *= value;
	}

	value = force_curve * drive_command;
	value *= config->acceleration;
	result->longitudinal = (float)(value + result->longitudinal);

	if (result->speed < LAUNCH_SPEED) {
		value = LAUNCH_SPEED - result->speed;
		value *= drive_command;
		value *= LAUNCH_FORCE;
		result->longitudinal = (float)(value + result->longitudinal);
	}

	return MH_ERROR_NONE;
}

static void
apply_handbrake(const struct mh_engine_config *config,
	const struct mh_engine_input *input, struct mh_engine_result *result)
{
	if (input->handbrake &&
		result->braking < config->brake_force * HANDBRAKE_FORCE_SHARE)
		result->braking = (float)(result->braking +
			config->brake_force * HANDBRAKE_FORCE_SHARE);
}

enum mh_error_code
mh_engine_step(const struct mh_engine_config *config,
	const struct mh_engine_state *state,
	const struct mh_engine_input *input,
	const struct mh_engine_feedback *feedback, double step_seconds,
	struct mh_engine_result *output, struct mh_error *error_out)
{
	struct mh_engine_result result;
	struct stored_engine_inputs stored;
	struct engine_curve curve;
	double ratio, drive_command, forward_velocity;
	volatile double speed;
	enum mh_error_code status;

	status = validate_step(config, state, input, feedback, step_seconds,
		output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	store_engine_inputs(config, state, input, feedback, &stored);
	config = &stored.config;
	state = &stored.state;
	input = &stored.controls;
	feedback = &stored.feedback;
	step_seconds = (float)step_seconds;

	memset(&result, 0, sizeof(result));
	result.state = *state;
	result.throttle = clamp(input->throttle, 0, 1);
	result.brake = clamp(input->brake, 0, 1);
	result.candidate_gear = pending_gear(state, config->gears);
	ratio = (float)config->ratio[state->gear];
	forward_velocity = feedback->forward_velocity;
	speed = forward_velocity * KMH_PER_MPS;

	/*
	 * Previous grip decides spin BEFORE heavy braking suppresses a pedal.
	 */
	evaluate_wheelspin(
		config, state, input, feedback, ratio, speed, &result);
	drive_command = prepare_pedals(config, state, &result);
	apply_automatic_engine_drag(
		config, state, ratio, forward_velocity, drive_command, &result);
	result.speed = (float)speed;

	if (result.speed < 0)
		result.speed = -result.speed;

	/*
	 * Eligibility drives RPM/force even when old detachment inhibits spin.
	 */
	if (result.spin_eligible)
		prepare_wheelspin_curve(config, state, feedback, drive_command,
			step_seconds, &result, &curve);
	else
		prepare_road_speed_curve(config, ratio, &result, &curve);

	status = apply_drive_force(
		config, feedback, &curve, drive_command, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!config->options.manual)
		shift_automatic(config, forward_velocity, curve.rpm, &result);
	else
		apply_manual_controls(config, state, input, ratio,
			forward_velocity, result.candidate_gear, &result);

	apply_handbrake(config, input, &result);
	update_rpm_and_detachment(config, state, feedback, drive_command,
		curve.rpm, step_seconds, &result);

	if (!bounded(result.longitudinal, FORCE_LIMIT) ||
		!(result.state.rpm >= 0 && result.state.rpm <= RPM_LIMIT) ||
		!bounded(result.spin_estimate, SPIN_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"engine result exceeds native bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
