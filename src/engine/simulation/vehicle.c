#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/body_internal.h"
#include "engine/simulation/grip_internal.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/vehicle_internal.h"
#include "engine/simulation/wheels.h"

#define FINAL_SUSPENSION_PASS (MH_MOTION_PASSES - 1)
#define REAR_RIGHT 2
#define REAR_LEFT 3

static int
rear_supported(const struct mh_grip_state *grip)
{
	return grip->wheels[REAR_RIGHT].supported ||
		grip->wheels[REAR_LEFT].supported;
}

enum mh_error_code
mh_vehicle_reset(const struct mh_engine_config *config,
	const double position[3], const double rotation[9],
	struct mh_vehicle_state *output, struct mh_error *error_out)
{
	struct mh_vehicle_state state;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"null vehicle reset output");

	memset(&state, 0, sizeof(state));
	status = mh_motion_reset(position, rotation, &state.motion, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_engine_reset(config, &state.engine, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	state.contact_origin_y = position[1];
	state.body_points = MH_CAR_BASE_POINTS;
	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_vehicle_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out)
{
	return mh_vehicle_step_tuned(car, col, materials, config, NULL, state,
		input, step_seconds, output, error_out);
}

enum mh_error_code
mh_vehicle_prepare_tuned(const struct mh_car *car,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out)
{
	struct mh_vehicle_result result;
	struct mh_engine_feedback feedback;
	struct mh_force_input force_input;
	struct mh_car tuned_car;
	enum mh_error_code status;

	if (car == NULL || state == NULL || input == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "null vehicle arguments");

	if (tuning != NULL) {
		tuned_car = *car;
		tuned_car.turn_force = tuning->turn_force;
		tuned_car.brake_force = tuning->brake_force;
		car = &tuned_car;
	}

	if (input->role > 2 ||
		(input->body_contact != 0 && input->body_contact != 1) ||
		(input->strong_air_damping != 0 &&
			input->strong_air_damping != 1) ||
		(state->body.contact != 0 && state->body.contact != 1) ||
		(state->body.strong_air_damping != 0 &&
			state->body.strong_air_damping != 1)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid vehicle body feedback or overrides");
	}

	memset(&result, 0, sizeof(result));

	/*
	 * Wheelspin and detachment use the previous tick's final rear support.
	 */
	feedback.forward_velocity = state->motion.velocity[2];
	feedback.rear_limit = state->grip.rear_limit;
	feedback.rear_supported = rear_supported(&state->grip);
	feedback.forced_detach = input->forced_detach;
	status = mh_engine_step(config, &state->engine, &input->controls,
		&feedback, step_seconds, &result.engine, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Current engine force/spin, but previous grip phase and slide state.
	 */
	force_input.steering = input->controls.steering;
	force_input.previous_steering = state->smoothed_steering;
	force_input.longitudinal = result.engine.longitudinal;
	force_input.phase = state->grip.phase;
	force_input.sliding = state->grip.sliding;
	force_input.wheelspin = result.engine.wheelspin;
	force_input.reduced_gravity = input->reduced_gravity;
	status = mh_force_prepare(
		car, &state->motion, &force_input, &result.forces, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.state = *state;
	result.state.engine = result.engine.state;
	result.state.speed = (float)result.engine.speed;
	result.state.smoothed_steering = result.forces.smoothed_steering;
	result.state.brake_active = result.engine.brake_active;
	result.state.wheelspin = result.engine.wheelspin;
	result.state.heavy_brake = result.engine.heavy_brake;
	result.state.engine_braking = result.engine.engine_braking;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_vehicle_road_step_tuned(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out)
{
	struct mh_vehicle_result result;
	struct mh_grip_control grip_control;
	struct mh_wheel_input wheel_input;
	struct mh_car tuned_car;
	const struct mh_suspension *final_pass;
	double grip_level;
	unsigned int wheel;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing vehicle output");

	status = mh_vehicle_prepare_tuned(car, config, tuning, state, input,
		step_seconds, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	grip_level = car->grip_level;

	if (tuning != NULL) {
		tuned_car = *car;
		tuned_car.turn_force = tuning->turn_force;
		tuned_car.brake_force = tuning->brake_force;
		car = &tuned_car;
		grip_level = tuning->grip_level;
	}

	/*
	 * Current engine outputs; body contact/damping still come from entry.
	 */
	grip_control.gear = result.engine.state.gear;
	grip_control.rpm = result.engine.state.rpm;
	grip_control.braking = result.engine.braking;
	grip_control.longitudinal = result.engine.longitudinal;
	grip_control.wheelspin = result.engine.wheelspin;
	grip_control.detached = result.engine.state.detached;
	grip_control.heavy_brake = result.engine.heavy_brake;
	grip_control.handbrake = input->controls.handbrake;
	grip_control.body_contact = input->body_contact || state->body.contact;
	grip_control.strong_air_damping =
		input->strong_air_damping || state->body.strong_air_damping;
	grip_control.forced_stop = input->forced_stop;
	status = mh_grip_road_step(car, col, materials, &state->motion,
		&result.forces.forces, &state->grip, &grip_control, grip_level,
		tuning == NULL ? NULL : tuning->suspension, step_seconds,
		&result.road, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.state.motion = result.road.motion.state;
	result.state.grip = result.road.grip.state;
	result.state.body_points =
		mh_body_point_count(result.road.grip.contacts, input->role);
	result.state.contact_origin_y = state->motion.position[1];
	final_pass = &result.road.motion.passes[FINAL_SUSPENSION_PASS];

	for (wheel = 0; wheel < 4; wheel++)
		result.state.wheel_contacts[wheel] =
			final_pass->wheels[wheel].contact;

	wheel_input.ratio = config->ratio[result.engine.state.gear];
	wheel_input.rpm = result.engine.state.rpm;
	wheel_input.gear = result.engine.state.gear;
	wheel_input.forward_velocity = result.road.grip.wheel_forward;
	wheel_input.heavy_brake = result.engine.heavy_brake;
	wheel_input.wheelspin = result.engine.wheelspin;
	wheel_input.rear_supported = rear_supported(&result.state.grip);
	wheel_input.up_ready = result.engine.state.up_ready;
	wheel_input.down_ready = result.engine.state.down_ready;
	status = mh_wheel_rates(
		&wheel_input, result.state.wheel_rate, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_vehicle_step_tuned(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_engine_config *config,
	const struct mh_vehicle_tuning *tuning,
	const struct mh_vehicle_state *state,
	const struct mh_vehicle_input *input, double step_seconds,
	struct mh_vehicle_result *output, struct mh_error *error_out)
{
	struct mh_vehicle_result result;
	struct mh_vehicle_state entry;
	enum mh_error_code status;

	if (state == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing vehicle state or output");

	if (!mh_motion_valid(&state->motion))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid vehicle motion state");

	entry = *state;
	mh_motion_enter(&entry.motion);
	status = mh_vehicle_road_step_tuned(car, col, materials, config, tuning,
		&entry, input, step_seconds, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_body_contact_step(car, col, &entry.motion,
		&result.road.motion.state, step_seconds,
		result.state.body_points, &result.body, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.state.motion = result.body.state;
	mh_motion_leave(&result.state.motion);

	if (!mh_motion_valid(&result.state.motion))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"vehicle result exceeds motion bounds");

	result.state.body = result.body.feedback;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
