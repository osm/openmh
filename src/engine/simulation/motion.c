#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/simulation/grip_internal.h"
#include "engine/simulation/motion.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/suspension_internal.h"

#define POSITION_LIMIT 1e6
#define VELOCITY_LIMIT 1e4
#define CAR_ANGULAR_LIMIT 1e3
#define OBJECT_ANGULAR_LIMIT 1e5
#define FORCE_LIMIT 1e9
#define STEP_LIMIT 0.1
#define LATERAL_HOLD_SPEED 0.7
#define FORWARD_HOLD_SPEED 3
#define DISPLAY_HEIGHT 0.3199999928474426
#define HISTORY_CORRECTION_SHARE 0.5

struct grip_stage {
	double					grip_level;
	const struct mh_suspension_config	*suspension;
	const struct mh_materials		*materials;
	const struct mh_grip_state		*state;
	const struct mh_grip_control		*control;
	struct mh_grip_prepared			*prepared;
	struct mh_grip_result			*result;
};

static int
bounded(double value, double limit)
{
	return value >= -limit && value <= limit;
}

static int
valid_pose(const double position[3], const double rotation[9])
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		if (!bounded(position[i], POSITION_LIMIT))
			return 0;

	return mh_rotation_valid(rotation);
}

static int
valid_state(const struct mh_motion_state *state, double angular_limit)
{
	unsigned int axis, wheel;

	for (axis = 0; axis < 3; axis++) {
		if (!bounded(state->position[axis], POSITION_LIMIT) ||
			!bounded(state->velocity[axis], VELOCITY_LIMIT) ||
			!bounded(state->previous_velocity[axis],
				VELOCITY_LIMIT) ||
			!bounded(
				state->angular_velocity[axis], angular_limit) ||
			!bounded(state->previous_angular_velocity[axis],
				angular_limit))
			return 0;
	}

	for (wheel = 0; wheel < 4; wheel++)
		if (!(state->extension[wheel] >= 0 &&
			state->extension[wheel] <= 1))
			return 0;

	return 1;
}

int
mh_motion_valid(const struct mh_motion_state *state)
{
	return mh_rotation_valid(state->rotation) &&
		valid_state(state, CAR_ANGULAR_LIMIT);
}

int
mh_motion_valid_object(const struct mh_motion_state *state)
{
	/*
	 * Original scenery does not normalize its basis between frames.
	 * Repeated binary32 contact stores can exceed the car's 1e-6 allowance.
	 * Permit accumulated drift while retaining bounds on scale, skew and
	 * determinant. Do not normalize here: that would change subsequent
	 * contacts.
	 */
	return mh_rotation_valid_accumulated(state->rotation) &&
		valid_state(state, OBJECT_ANGULAR_LIMIT);
}

void
mh_motion_accelerate(struct mh_motion_state *state, double mass,
	const double force[3], const double torque[3], double pass_seconds)
{
	static const double inertia[3] = {3500, 2000, 1500};
	volatile double value;
	double scale, seconds;
	unsigned int axis;

	/*
	 * Linear integration stores h/m as binary32 before scaling force.
	 * Angular integration retains 53-bit reciprocal/product intermediates.
	 * All velocity inputs and final stores are binary32.
	 */
	seconds = (double)(float)pass_seconds;
	value = 1 / (double)(float)mass;
	value *= seconds;
	scale = (float)value;

	for (axis = 0; axis < 3; axis++) {
		value = (double)(float)force[axis] * scale;
		value += (double)(float)state->velocity[axis];
		state->velocity[axis] = (float)value;
		value = 1 / inertia[axis];
		value *= (double)(float)torque[axis];
		value *= seconds;
		value += (double)(float)state->angular_velocity[axis];
		state->angular_velocity[axis] = (float)value;
	}

	if (fabs(state->velocity[0]) < LATERAL_HOLD_SPEED &&
		fabs(state->velocity[2]) < FORWARD_HOLD_SPEED)
		state->velocity[0] = 0;
}

void
mh_motion_translate(struct mh_motion_state *state, const double local[3])
{
	unsigned int axis, component;
	double position;

	/*
	 * Each local-axis call reads binary32 pose and distance values, then
	 * stores all three position components. Used after rotation.
	 */
	for (axis = 0; axis < 3; axis++) {
		position = (double)(float)state->position[axis];

		for (component = 0; component < 3; component++)
			position = (float)(position +
				(double)(float)state->rotation[component * 3 +
					axis] *
					(double)(float)local[component]);

		state->position[axis] = position;
	}
}

/* Rotate, translate in the new local basis, then rebase linear velocity. */
void
mh_motion_apply(struct mh_motion_state *state, const double angle[3],
	const double translation[3])
{
	double delta_rotation[9];
	double rebased_velocity[3];
	volatile double value;
	unsigned int i;
	unsigned int first, second;

	memset(delta_rotation, 0, sizeof(delta_rotation));
	delta_rotation[0] = delta_rotation[4] = delta_rotation[8] = 1;
	mh_motion_rotate_pair(state->rotation, delta_rotation, angle);
	mh_motion_translate(state, translation);

	for (i = 0; i < 3; i++) {
		/* Stored inputs, two additions, then a float store. */
		first = i == 0 ? 1 : 0;
		second = 1 - first;
		value = delta_rotation[i * 3 + first] *
			(double)(float)state->velocity[first];
		value += delta_rotation[i * 3 + second] *
			(double)(float)state->velocity[second];
		value += delta_rotation[i * 3 + 2] *
			(double)(float)state->velocity[2];
		rebased_velocity[i] = (float)value;
	}

	memcpy(state->velocity, rebased_velocity, sizeof(rebased_velocity));
}

enum mh_error_code
mh_motion_reset(const double position[3], const double rotation[9],
	struct mh_motion_state *output, struct mh_error *error_out)
{
	struct mh_motion_state result;
	unsigned int wheel;

	if (position == NULL || rotation == NULL || output == NULL ||
		!valid_pose(position, rotation))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid motion reset pose");

	memset(&result, 0, sizeof(result));
	memcpy(result.position, position, sizeof(result.position));
	memcpy(result.rotation, rotation, sizeof(result.rotation));

	for (wheel = 0; wheel < 4; wheel++)
		result.extension[wheel] = 1;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_motion_correct_history(struct mh_motion_state *state, double step_seconds)
{
	double angle[3], translation[3];
	volatile double value;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		/* Binary32 inputs and helper arguments. */
		value = (double)(float)state->angular_velocity[i] -
			(double)(float)state->previous_angular_velocity[i];
		value *= HISTORY_CORRECTION_SHARE;
		value *= (double)(float)step_seconds;
		angle[i] = (float)value;
		value = (double)(float)state->velocity[i] -
			(double)(float)state->previous_velocity[i];
		value *= HISTORY_CORRECTION_SHARE;
		value *= (double)(float)step_seconds;
		translation[i] = (float)value;

		state->previous_velocity[i] = (float)state->velocity[i];
		state->previous_angular_velocity[i] =
			(float)state->angular_velocity[i];
	}

	mh_motion_apply(state, angle, translation);
}

static enum mh_error_code
integrate_motion(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *input,
	const struct mh_motion_forces *forces, double step_seconds,
	struct mh_motion_result *output, const struct grip_stage *grip,
	int normalize, struct mh_error *error_out)
{
	struct mh_motion_result result;
	struct mh_suspension_input query;
	struct mh_motion_state *state;
	const struct mh_suspension *previous;
	enum mh_error_code status;
	double angle[3];
	double translation[3];
	double force[3], torque[3];
	double pass_seconds;
	unsigned int axis;
	unsigned int wheel;
	unsigned int pass;

	if (car == NULL || col == NULL || input == NULL || forces == NULL ||
		output == NULL ||
		!(step_seconds > 0 && step_seconds <= STEP_LIMIT) ||
		!mh_motion_valid(input))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid motion input or timestep");

	for (axis = 0; axis < 3; axis++) {
		if (!bounded(forces->body_force[axis], FORCE_LIMIT) ||
			!bounded(forces->body_torque[axis], FORCE_LIMIT)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid body force or torque");
		}

		for (wheel = 0; wheel < 4; wheel++) {
			if (!bounded(forces->traction[wheel][axis],
				FORCE_LIMIT) ||
				!bounded(forces->applied[wheel][axis],
					FORCE_LIMIT)) {
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid motion wheel force");
			}
		}
	}

	memset(&result, 0, sizeof(result));
	result.state = *input;
	state = &result.state;

	mh_motion_correct_history(state, step_seconds);

	if (grip != NULL) {
		status = mh_grip_prepare(grip->materials, grip->state,
			grip->control, state, input->previous_velocity[2],
			forces, step_seconds, grip->prepared, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		forces = &grip->prepared->forces;
	}

	memcpy(query.traction, forces->traction, sizeof(query.traction));
	memcpy(query.applied, forces->applied, sizeof(query.applied));
	pass_seconds = (float)((double)(float)step_seconds / MH_MOTION_PASSES);

	for (pass = 0; pass < MH_MOTION_PASSES; pass++) {
		if (!mh_motion_valid(state))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"motion state exceeds native bounds");

		memcpy(query.position, state->position, sizeof(query.position));
		memcpy(query.rotation, state->rotation, sizeof(query.rotation));
		memcpy(query.previous_extension, state->extension,
			sizeof(query.previous_extension));

		/*
		 * Clear support at motion entry. Reuse a supported wheel's
		 * plane unless the descending pass counter is 1. Never carry
		 * this cache into a new step.
		 */
		previous = pass != 0 && MH_MOTION_PASSES - pass - 1 != 1
			? &result.passes[pass - 1]
			: NULL;
		status = mh_suspension_evaluate_cached(car, col,
			grip == NULL ? NULL : grip->suspension, &query,
			previous, &result.passes[pass], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (axis = 0; axis < 3; axis++) {
			force[axis] = (float)forces->body_force[axis];
			torque[axis] = (float)forces->body_torque[axis];
		}

		for (wheel = 0; wheel < 4; wheel++) {
			state->extension[wheel] =
				result.passes[pass].wheels[wheel].extension;
			mh_suspension_accumulate(
				&result.passes[pass].wheels[wheel], force,
				torque);
		}

		mh_motion_accelerate(
			state, car->mass, force, torque, pass_seconds);

		if (!mh_motion_valid(state))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"motion velocity exceeds native bounds");

		for (axis = 0; axis < 3; axis++) {
			angle[axis] =
				state->angular_velocity[axis] * pass_seconds;
			translation[axis] =
				state->velocity[axis] * pass_seconds;
		}

		mh_motion_apply(state, angle, translation);
	}

	if (!mh_motion_valid(state))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"motion result exceeds native bounds");

	if (grip != NULL) {
		status = mh_grip_finish_tuned(car, grip->materials,
			&grip->prepared->state, grip->control, state,
			&result.passes[MH_MOTION_PASSES - 1], grip->grip_level,
			step_seconds, grip->result, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memcpy(state->velocity, grip->result->velocity,
			sizeof(state->velocity));
		memcpy(state->angular_velocity, grip->result->angular_velocity,
			sizeof(state->angular_velocity));
	}

	if (normalize)
		mh_rotation_normalize(state->rotation);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_motion_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_motion_state *input,
	const struct mh_motion_forces *forces, double step_seconds,
	struct mh_motion_result *output, struct mh_error *error_out)
{
	return integrate_motion(car, col, input, forces, step_seconds, output,
		NULL, 1, error_out);
}

enum mh_error_code
mh_grip_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out)
{
	return mh_grip_step_tuned(car, col, materials, motion, forces, grip,
		control, car == NULL ? -1 : (double)car->grip_level,
		step_seconds, output, error_out);
}

enum mh_error_code
mh_grip_step_tuned(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	double step_seconds, struct mh_grip_step_result *output,
	struct mh_error *error_out)
{
	return mh_grip_step_configured(car, col, materials, motion, forces,
		grip, control, grip_level, NULL, step_seconds, output,
		error_out);
}

static enum mh_error_code
grip_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	const struct mh_suspension_config *suspension, double step_seconds,
	int normalize, struct mh_grip_step_result *output,
	struct mh_error *error_out)
{
	struct mh_grip_step_result result;
	struct grip_stage stage;
	enum mh_error_code status;

	if (materials == NULL || grip == NULL || control == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid grip step arguments");

	stage.materials = materials;
	stage.suspension = suspension;
	stage.grip_level = grip_level;
	stage.state = grip;
	stage.control = control;
	stage.prepared = &result.prepared;
	stage.result = &result.grip;
	status = integrate_motion(car, col, motion, forces, step_seconds,
		&result.motion, &stage, normalize, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_grip_step_configured(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	const struct mh_suspension_config *suspension, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out)
{
	return grip_step(car, col, materials, motion, forces, grip, control,
		grip_level, suspension, step_seconds, 1, output, error_out);
}

enum mh_error_code
mh_grip_road_step(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials,
	const struct mh_motion_state *motion,
	const struct mh_motion_forces *forces, const struct mh_grip_state *grip,
	const struct mh_grip_control *control, double grip_level,
	const struct mh_suspension_config *suspension, double step_seconds,
	struct mh_grip_step_result *output, struct mh_error *error_out)
{
	return grip_step(car, col, materials, motion, forces, grip, control,
		grip_level, suspension, step_seconds, 0, output, error_out);
}

enum mh_error_code
mh_motion_display(const struct mh_motion_state *state, double position[3],
	double world_velocity[3], struct mh_error *error_out)
{
	double display_position[3];
	double display_velocity[3];
	unsigned int i;
	unsigned int j;

	if (state == NULL || position == NULL || world_velocity == NULL ||
		!mh_motion_valid(state)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid motion display arguments");
	}

	for (i = 0; i < 3; i++) {
		display_position[i] = state->position[i] +
			DISPLAY_HEIGHT * state->rotation[3 + i];

		display_velocity[i] = 0;

		for (j = 0; j < 3; j++)
			display_velocity[i] +=
				state->rotation[j * 3 + i] * state->velocity[j];
	}

	memcpy(position, display_position, sizeof(display_position));
	memcpy(world_velocity, display_velocity, sizeof(display_velocity));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
