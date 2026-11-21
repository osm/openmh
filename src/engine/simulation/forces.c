#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/forces.h"

#define MASS_LIMIT 10000
#define FORCE_LIMIT 1e9
#define STEERING_LIMIT 1e12
#define LONGITUDINAL_LIMIT 1e8
#define PHASE_LIMIT 1e9
#define GRAVITY 15.8100004196167
#define REDUCED_GRAVITY_SCALE 0.5
#define STEERING_SMOOTHING 0.2
#define STEERING_RAMP_SPEED 4
#define SPEED_LOAD_SCALE 0.05
#define WHEEL_WEIGHT_SHARE 0.25
#define FRONT_LOAD_SHARE 0.3
#define REAR_LOAD_SHARE 0.2
#define SPIN_PHASE_SCALE 0.033
#define SPIN_SPLIT_SCALE 0.25

static int
bounded(double value, double limit)
{
	return value >= -limit && value <= limit;
}

static double
clamp_steering(double value)
{
	if (value < -1)
		return -1;

	if (value > 1)
		return 1;

	return value;
}

static void
apply_body_loads(const struct mh_car *car, const struct mh_motion_state *motion,
	struct mh_force_result *result)
{
	double gravity_local, speed_load_local, gravity_world, speed_load_world;
	double mass, speed, rotation[9];
	volatile double value;
	unsigned int axis, wheel;

	/* Store world loads before transposing. */
	mass = (float)car->mass;
	speed = (float)motion->velocity[2];
	gravity_world = (float)(-result->gravity * mass);
	value = mass * -SPEED_LOAD_SCALE;
	value *= speed;
	speed_load_world = (float)value;

	for (axis = 0; axis < 9; axis++)
		rotation[axis] = (float)motion->rotation[axis];

	for (axis = 0; axis < 3; axis++) {
		/*
		 * R transpose times a world-Y vector: the Y row of each column.
		 * Preserve the helper's zero products too: they determine
		 * signed zero. The speed load uses signed local forward speed.
		 */
		gravity_local =
			(float)((gravity_world * rotation[axis * 3 + 1] +
					0 * rotation[axis * 3]) +
				0 * rotation[axis * 3 + 2]);
		speed_load_local =
			(float)((speed_load_world * rotation[axis * 3 + 1] +
					0 * rotation[axis * 3]) +
				0 * rotation[axis * 3 + 2]);
		gravity_local = (float)(gravity_local * WHEEL_WEIGHT_SHARE);

		for (wheel = 0; wheel < 4; wheel++) {
			/* Stored gravity quarter, then extra load. */
			value = speed_load_local *
				(wheel < 2 ? FRONT_LOAD_SHARE
					: REAR_LOAD_SHARE);
			value += gravity_local;
			result->forces.applied[wheel][axis] = (float)value;
		}
	}
}

static enum mh_error_code
prepare_traction(const struct mh_force_input *input,
	struct mh_force_result *result, struct mh_error *error_out)
{
	double spin_split;
	volatile double value;
	unsigned int wheel, axis;

	spin_split = 0;

	if (input->wheelspin) {
		/* Binary53 argument and axle split, no float store. */
		value = (double)(float)input->phase * SPIN_PHASE_SCALE;
		value = sin(value);
		value += 1;
		spin_split = value * SPIN_SPLIT_SCALE;
	}

	for (wheel = 0; wheel < 4; wheel++) {
		if (wheel < 2) {
			value = result->steering_force *
				(input->wheelspin ? spin_split + 0.5 : 1);
			result->forces.traction[wheel][0] = (float)value;
		} else {
			if (input->wheelspin) {
				value = result->steering_force *
					(spin_split - 1);
				result->forces.traction[wheel][0] =
					(float)value;
			}

			result->forces.traction[wheel][2] =
				(float)((double)(float)input->longitudinal * 2);
		}

		for (axis = 0; axis < 3; axis++) {
			if (!bounded(result->forces.traction[wheel][axis],
				FORCE_LIMIT) ||
				!bounded(result->forces.applied[wheel][axis],
					FORCE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"prepared forces exceed native bounds");
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
prepare_steering(const struct mh_car *car, const struct mh_motion_state *motion,
	const struct mh_force_input *input, struct mh_force_result *result)
{
	double speed, previous;
	volatile double value;

	result->steering = clamp_steering((double)(float)input->steering);
	previous = (float)input->previous_steering;

	/* Store the per-call steering history. */
	value = (result->steering - previous) * STEERING_SMOOTHING;
	value += previous;
	result->smoothed_steering = clamp_steering((double)(float)value);

	/*
	 * History is visual; physical steering uses the current clamped input.
	 */
	speed = (float)motion->velocity[2];
	value = result->steering * (double)(float)car->turn_force;

	if (fabs(speed) < STEERING_RAMP_SPEED) {
		value /= STEERING_RAMP_SPEED;
		value *= fabs(speed);
	}

	/* Store the force before reversal or wheelspin splitting. */
	result->steering_force = (float)value;

	if (speed < 0 && !input->sliding)
		result->steering_force = -result->steering_force;
}

enum mh_error_code
mh_force_prepare(const struct mh_car *car, const struct mh_motion_state *motion,
	const struct mh_force_input *input, struct mh_force_result *output,
	struct mh_error *error_out)
{
	struct mh_force_result result;
	enum mh_error_code status;
	double unused_position[3];
	double unused_velocity[3];

	if (car == NULL || motion == NULL || input == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid force arguments");

	if (!car->has_suspension || !car->has_steering ||
		!(car->mass > 0 && car->mass <= MASS_LIMIT) ||
		!(car->turn_force >= 0 && car->turn_force <= FORCE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car force settings");

	if (!bounded(input->steering, STEERING_LIMIT) ||
		!bounded(input->previous_steering, 1) ||
		!bounded(input->longitudinal, LONGITUDINAL_LIMIT) ||
		!bounded(input->phase, PHASE_LIMIT) ||
		(input->sliding != 0 && input->sliding != 1) ||
		(input->wheelspin != 0 && input->wheelspin != 1) ||
		(input->reduced_gravity != 0 && input->reduced_gravity != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid force input");

	/*
	 * Share motion's full state validation without exposing a second set
	 * of pose/history bounds. This conversion has no state side effects.
	 */
	status = mh_motion_display(
		motion, unused_position, unused_velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&result, 0, sizeof(result));
	result.gravity = GRAVITY;

	if (input->reduced_gravity)
		result.gravity *= REDUCED_GRAVITY_SCALE;

	prepare_steering(car, motion, input, &result);

	apply_body_loads(car, motion, &result);

	status = prepare_traction(input, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
