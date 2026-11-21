#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/grip.h"

#define MATERIAL_ID_LIMIT 25
#define PHASE_LIMIT 1e9
#define SLIDE_LIMIT 1e12
#define RATE_LIMIT 1e6
#define BRAKE_LIMIT 1000
#define LONGITUDINAL_LIMIT 1e8
#define FORCE_LIMIT 1e9
#define ROTATION_SLIP_LIMIT 1e9
#define VELOCITY_LIMIT 1e4
#define GEAR_LIMIT 9
#define STEP_LIMIT .1
#define BUMP_LIMIT 100
#define NORMAL_MINIMUM 1e-45
#define NORMAL_LIMIT 1.000001
#define BRAKE_FORCE_MINIMUM 1e-6
#define LOW_SPEED_BRAKE_LIMIT 6
#define BRAKE_FORCE_REFERENCE 1.1
#define BRAKE_SPEED_LOSS_SCALE .007
#define GRIP_LEVEL_LIMIT 1000
#define GRIP_LEVEL_MAXIMUM 10
#define PHASE_WRAP_THRESHOLD 804.24771931904
#define PHASE_WRAP_AMOUNT 804.2477416992188
#define TRANSFER_LIMIT 8000
#define BUMP_TORQUE_LIMIT 2000
#define SLIDE_STEERING_LIMIT 10
#define WHEELSPIN_STEERING_SCALE .8
#define FRICTION_CAP_THRESHOLD .99999
#define FRICTION_CAP_VALUE .9999899864196777
#define ZERO_NORMAL_RECIPROCAL 10000
#define BODY_FRICTION_SCALE .4
#define TILT_FRICTION 4.999999873689376e-5
#define HOLD_SPEED .7
#define HOLD_FORCE 500
#define HOLD_FRICTION .8
#define FIRST_REAR_WHEEL 2

enum grip_scale {
	SLIP_SPEED_SCALE,
	SLIP_ROTATION_SCALE,
	LATERAL_FRICTION_SCALE,
	YAW_FRICTION_SCALE,
	GRIP_SCALES
};

/* Rolling/sliding material sums, then averaged damping bases. */
struct contact_grip {
	double	speed_limit, rotation_limit;
	double	lateral[2], longitudinal[2], yaw[2];
};

/* Supported-contact averaging and the car's grip-level multipliers. */
struct supported_grip_scales {
	double	reciprocal, lateral, yaw;
};

static int
bounded(double value, double limit)
{
	return value >= -limit && value <= limit;
}

static int
nonnegative(double value, double limit)
{
	return value >= 0 && value <= limit;
}

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

static const struct mh_material *
lookup_material(const struct mh_materials *table, unsigned long id)
{
	if (id > MATERIAL_ID_LIMIT)
		id = 0;

	return id < table->count ? &table->entries[id] : NULL;
}

static int
valid_material(const struct mh_material *surface)
{
	return nonnegative(surface->spin_speed, RATE_LIMIT) &&
		nonnegative(surface->spin_rot, ROTATION_SLIP_LIMIT) &&
		nonnegative(surface->x_spin, 1) &&
		nonnegative(surface->x_roll, 1) &&
		nonnegative(surface->z_spin, 1) &&
		nonnegative(surface->z_roll, 1) &&
		nonnegative(surface->rot_spin, 1) &&
		nonnegative(surface->rot_roll, 1) &&
		nonnegative(surface->turn_spin, 1) &&
		nonnegative(surface->turn_roll, 1) &&
		nonnegative(surface->bumpy, BUMP_LIMIT) &&
		nonnegative(surface->wheel_spin, RATE_LIMIT) &&
		nonnegative(surface->wheel_roll, RATE_LIMIT);
}

static int
valid_normal(double y)
{
	/*
	 * COL normals are binary32. Accept every nonzero binary32 magnitude;
	 * the stored cube can underflow to zero and select the 10000 fallback.
	 * Test its stored cube for zero, with no small-Ny rejection. Preserve
	 * the binary32 cube and reciprocal stores.
	 */
	return y == 0 || (fabs(y) >= NORMAL_MINIMUM && fabs(y) <= NORMAL_LIMIT);
}

static enum mh_error_code
validate_inputs(const struct mh_materials *table,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion, double step_seconds,
	struct mh_error *error_out)
{
	double display_position[3], display_velocity[3];
	unsigned int material, wheel;

	if (table == NULL || state == NULL || control == NULL ||
		motion == NULL || table->count == 0 ||
		table->count > MH_MATERIAL_LIMIT ||
		!(step_seconds > 0 && step_seconds <= STEP_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid grip arguments");

	if (!bounded(state->phase, PHASE_LIMIT) ||
		!valid_flag(state->sliding) ||
		!nonnegative(state->slide_amount, SLIDE_LIMIT) ||
		!nonnegative(state->rear_limit, RATE_LIMIT) ||
		control->gear > GEAR_LIMIT ||
		!nonnegative(control->rpm, RATE_LIMIT) ||
		!nonnegative(control->braking, BRAKE_LIMIT) ||
		!bounded(control->longitudinal, LONGITUDINAL_LIMIT) ||
		!valid_flag(control->wheelspin) ||
		!valid_flag(control->detached) ||
		!valid_flag(control->heavy_brake) ||
		!valid_flag(control->handbrake) ||
		!valid_flag(control->body_contact) ||
		!valid_flag(control->strong_air_damping) ||
		!valid_flag(control->forced_stop))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid grip state or controls");

	for (material = 0; material < table->count; material++)
		if (!valid_material(&table->entries[material]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material physics");

	for (wheel = 0; wheel < 4; wheel++) {
		if (!valid_flag(state->wheels[wheel].supported))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid previous wheel support");

		if ((state->wheels[wheel].material != MH_GRIP_NO_MATERIAL &&
			lookup_material(table,
				state->wheels[wheel].material) == NULL) ||
			(state->wheels[wheel].supported &&
				!valid_normal(state->wheels[wheel].normal_y)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid previous wheel material or normal");
	}

	return mh_motion_display(
		motion, display_position, display_velocity, error_out);
}

static int
valid_forces(const struct mh_motion_forces *forces)
{
	unsigned int axis, wheel;

	for (axis = 0; axis < 3; axis++) {
		if (!bounded(forces->body_force[axis], FORCE_LIMIT) ||
			!bounded(forces->body_torque[axis], FORCE_LIMIT))
			return 0;

		for (wheel = 0; wheel < 4; wheel++) {
			if (!bounded(forces->traction[wheel][axis],
				FORCE_LIMIT) ||
				!bounded(forces->applied[wheel][axis],
					FORCE_LIMIT))
				return 0;
		}
	}

	return 1;
}

/* The original compares the unrounded upper bound, then reloads its float. */
static double
stored_clamp(double value, double low, double high)
{
	double stored;

	stored = (float)value;

	if (value > high)
		stored = high;

	return stored < low ? low : stored;
}

/* Phase advances twice per valid material, including smooth materials. */
static void
apply_surface_bumps(const struct mh_material *surface, double speed,
	double step_seconds, double *phase, struct mh_motion_forces *forces)
{
	double bump, next_phase;
	volatile double value;

	/* Stored bump, material product, stored torque sum. */
	value = *phase * .75;
	value += speed;
	value = sin(value) * (speed * 80);
	bump = stored_clamp(value, -BUMP_TORQUE_LIMIT, BUMP_TORQUE_LIMIT);
	value = (double)(float)surface->bumpy * bump;
	value += forces->body_torque[0];
	forces->body_torque[0] = (float)value;

	/* Store phase but retain its unrounded value for cosine. */
	value = step_seconds * 7.6;
	value += *phase;
	next_phase = value;
	*phase = (float)next_phase;
	value = next_phase * .63;
	value += speed;
	value += .7;
	value = cos(value) * (speed * 140);
	bump = stored_clamp(value, -BUMP_TORQUE_LIMIT, BUMP_TORQUE_LIMIT);
	value = (double)(float)surface->bumpy * bump;
	value += forces->body_torque[2];
	forces->body_torque[2] = (float)value;
	*phase = (float)(*phase + (double)(float)(step_seconds * 8));
}

static double
contact_steering_scale(
	const struct mh_material *surface, const struct mh_grip_state *state)
{
	double amount, scale;

	amount = (float)state->slide_amount;
	scale = state->sliding ? (double)(float)surface->turn_spin *
			(amount <= SLIDE_STEERING_LIMIT
					? 1
					: (double)(float)(SLIDE_STEERING_LIMIT /
						amount))
			: (double)(float)surface->turn_roll;

	scale = (float)scale;

	return scale;
}

static void
apply_wheelspin_shake(double phase, double speed, double *lateral_force)
{
	double half_phase, shake;
	volatile double value;

	half_phase = phase * .5;
	value = half_phase * .26;
	value += .8;
	value = sin(value) * .73;
	shake = sin(value) * ((speed > 5 ? 5 : speed) * 500);
	value = half_phase * .11;
	value += 1.6;
	value = shake + sin(value);
	shake = value;
	value = half_phase * .06;
	value += 4.7;
	value = shake + sin(value);
	value += *lateral_force;
	*lateral_force = (float)value;
}

static void
prepare_contact(const struct mh_materials *materials,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control, unsigned int wheel_index,
	double speed, double step_seconds, double *phase,
	struct mh_grip_prepared *result)
{
	const struct mh_material *surface;
	double scale;

	surface =
		lookup_material(materials, state->wheels[wheel_index].material);
	apply_surface_bumps(
		surface, speed, step_seconds, phase, &result->forces);
	scale = contact_steering_scale(surface, state);

	if (control->wheelspin && control->gear > 0 && !control->detached) {
		scale = (float)(scale * WHEELSPIN_STEERING_SCALE);

		if (wheel_index >= FIRST_REAR_WHEEL)
			apply_wheelspin_shake(*phase, speed,
				&result->forces.applied[wheel_index][0]);
	}

	result->x_scale[wheel_index] = scale;

	if (*phase > PHASE_WRAP_THRESHOLD)
		*phase = (float)(*phase - PHASE_WRAP_AMOUNT);
}

enum mh_error_code
mh_grip_prepare(const struct mh_materials *materials,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *corrected, double previous_forward,
	const struct mh_motion_forces *forces, double step_seconds,
	struct mh_grip_prepared *output, struct mh_error *error_out)
{
	struct mh_grip_prepared result;
	enum mh_error_code status;
	double phase, speed, transfer, roll, load;
	volatile double value;
	unsigned int supported, wheel, axis;

	status = validate_inputs(
		materials, state, control, corrected, step_seconds, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (forces == NULL || output == NULL ||
		!bounded(previous_forward, VELOCITY_LIMIT) ||
		!valid_forces(forces))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid grip force preparation input");

	memset(&result, 0, sizeof(result));
	result.forces = *forces;
	result.state = *state;

	/* Inputs occupy binary32 runtime fields before the material stage. */
	for (axis = 0; axis < 3; axis++) {
		result.forces.body_force[axis] =
			(float)forces->body_force[axis];
		result.forces.body_torque[axis] =
			(float)forces->body_torque[axis];

		for (wheel = 0; wheel < 4; wheel++) {
			result.forces.traction[wheel][axis] =
				(float)forces->traction[wheel][axis];
			result.forces.applied[wheel][axis] =
				(float)forces->applied[wheel][axis];
		}
	}

	if (control->forced_stop)
		memset(result.forces.traction, 0,
			sizeof(result.forces.traction));

	phase = (float)state->phase;
	speed = (float)corrected->velocity[2];
	step_seconds = (float)step_seconds;
	supported = 0;

	for (wheel = 0; wheel < 4; wheel++)
		supported += state->wheels[wheel].supported;

	/* The original repeats transfer and roll torque in its wheel loop. */
	value = ((double)(float)previous_forward - speed) / step_seconds;
	value *= 500 - speed * 3;
	transfer = stored_clamp(value, -TRANSFER_LIMIT, TRANSFER_LIMIT);
	value = (double)(float)corrected->velocity[0] *
		(state->sliding ? 10 : 80);
	roll = value * sqrt(fabs(speed));

	for (wheel = 0; wheel < 4; wheel++) {
		result.x_scale[wheel] = 1;

		/* Test material != -1, independently of support. */
		if (state->wheels[wheel].material != MH_GRIP_NO_MATERIAL)
			prepare_contact(materials, state, control, wheel, speed,
				step_seconds, &phase, &result);

		result.forces.traction[wheel][0] =
			(float)(result.forces.traction[wheel][0] *
				result.x_scale[wheel]);

		if (supported == 4) {
			if (transfer > 0)
				load = wheel < FIRST_REAR_WHEEL ? -transfer
								: transfer * .5;
			else
				load = wheel < FIRST_REAR_WHEEL ? -transfer * .5
								: transfer;
			result.forces.applied[wheel][1] =
				(float)(result.forces.applied[wheel][1] + load);

			if (speed != 0)
				result.forces.body_torque[2] =
					(float)(result.forces.body_torque[2] -
						roll);
		}
	}

	result.state.phase = phase;

	if (!valid_forces(&result.forces))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"material-adjusted forces exceed native bounds");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* GripLevel interpolates four binary32 table endpoints. */
static void
grip_scales(double grip_level, double scales[GRIP_SCALES])
{
	static const double low[GRIP_SCALES] = {
		.75, .75, 1.2999999523162842, 1.100000023841858};

	static const double high[GRIP_SCALES] = {
		1.2999999523162842, 1.2999999523162842, .75, .949999988079071};

	unsigned int i;
	volatile double value;

	if (grip_level > GRIP_LEVEL_MAXIMUM)
		grip_level = GRIP_LEVEL_MAXIMUM;

	for (i = 0; i < GRIP_SCALES; i++) {
		value = (high[i] - low[i]) * .1;
		value *= grip_level;
		value += low[i];
		scales[i] = (float)value;
	}
}

static double
cap_friction(double value)
{
	return value > FRICTION_CAP_THRESHOLD ? FRICTION_CAP_VALUE : value;
}

static enum mh_error_code
collect_contacts(const struct mh_materials *materials,
	const struct mh_grip_control *control,
	const struct mh_suspension *contacts, struct contact_grip *grip,
	struct mh_grip_result *result, struct mh_error *error_out)
{
	const struct mh_material *surface;
	const struct mh_suspension_wheel *wheel;
	double normal_y, normal_reciprocal, cube;
	volatile double value;
	unsigned int wheel_index, rear_count;

	rear_count = 0;
	grip->speed_limit = grip->rotation_limit = 0;
	grip->lateral[0] = grip->lateral[1] = 0;
	grip->longitudinal[0] = grip->longitudinal[1] = 0;
	grip->yaw[0] = grip->yaw[1] = 0;

	for (wheel_index = 0; wheel_index < 4; wheel_index++) {
		wheel = &contacts->wheels[wheel_index];

		if (!valid_flag(wheel->supported))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid final wheel support");

		memset(&result->state.wheels[wheel_index], 0,
			sizeof(result->state.wheels[wheel_index]));
		result->state.wheels[wheel_index].material =
			MH_GRIP_NO_MATERIAL;

		if (!wheel->supported)
			continue;

		surface = lookup_material(materials, wheel->contact.material);
		normal_y = wheel->contact.normal[1];

		if (!wheel->contact.hit || surface == NULL ||
			!valid_normal(normal_y))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid final wheel material or normal");

		normal_y = (float)normal_y;
		result->state.wheels[wheel_index].supported = 1;
		result->state.wheels[wheel_index].material =
			wheel->contact.material;
		result->state.wheels[wheel_index].normal_y = normal_y;
		result->contacts++;

		/*
		 * Store the cube and reciprocal, then store each material
		 * accumulator after every wheel.
		 */
		value = normal_y * normal_y;
		cube = (float)(value * normal_y);
		normal_reciprocal = cube == 0 ? ZERO_NORMAL_RECIPROCAL
					: (double)(float)(1 / cube);
		grip->speed_limit = (float)(grip->speed_limit +
			(double)(float)surface->spin_speed);
		grip->rotation_limit = (float)(grip->rotation_limit +
			(double)(float)surface->spin_rot);
		value = (double)(float)surface->x_roll * normal_reciprocal;
		value += grip->lateral[0];
		grip->lateral[0] = (float)value;
		value = (double)(float)surface->x_spin * normal_reciprocal;
		value += grip->lateral[1];
		grip->lateral[1] = (float)value;
		grip->longitudinal[0] = (float)(grip->longitudinal[0] +
			(double)(float)surface->z_roll);
		grip->longitudinal[1] = (float)(grip->longitudinal[1] +
			(double)(float)surface->z_spin);
		grip->yaw[0] = (float)(grip->yaw[0] +
			(double)(float)surface->rot_roll);
		grip->yaw[1] = (float)(grip->yaw[1] +
			(double)(float)surface->rot_spin);

		if (wheel_index >= FIRST_REAR_WHEEL) {
			rear_count++;
			result->state.rear_limit =
				(float)(result->state.rear_limit +
					(double)(float)(control->wheelspin
							? surface->wheel_spin
							: surface->wheel_roll));
		}
	}

	if (rear_count == 2)
		result->state.rear_limit =
			(float)(result->state.rear_limit * .5);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
damp_airborne(const struct mh_grip_control *control, double step_seconds,
	struct mh_grip_result *result)
{
	double damping;
	unsigned int i;

	result->state.sliding = 0;

	/*
	 * Air branch preserves the old slide amount. Retained inverted body
	 * contact selects the separate strong damping path.
	 */
	damping = pow(control->strong_air_damping ? .001 : .99, step_seconds);
	result->velocity_scale[0] = result->velocity_scale[2] = damping;

	if (control->strong_air_damping)
		result->velocity_scale[1] = damping;

	damping = pow(control->strong_air_damping ? .01 : .95, step_seconds);

	for (i = 0; i < 3; i++)
		result->angular_scale[i] = damping;
}

/* Later causes replace the slide amount; reverse cancels every request. */
static void
select_slide(const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion, const struct contact_grip *grip,
	double speed, struct mh_grip_result *result)
{
	double amount, lateral_slip, rotation_slip;
	volatile double value;
	int sliding;

	sliding = control->handbrake;
	amount = sliding ? 0 : (double)(float)state->slide_amount;
	lateral_slip = (float)(fabs(motion->velocity[0]) - grip->speed_limit);
	value = motion->velocity[0] * motion->velocity[0];
	value += motion->velocity[2] * motion->velocity[2];
	rotation_slip = (float)(value * fabs(motion->angular_velocity[1]));

	if (lateral_slip > 0) {
		amount = lateral_slip;
		sliding = 1;
	}

	if (rotation_slip > grip->rotation_limit) {
		amount = (float)(rotation_slip * .001);
		sliding = 1;
	}

	if (control->wheelspin) {
		value = control->gear * 500 + (double)(float)control->rpm;
		amount = (float)(value * .0007);
		sliding = 1;
	}

	if (control->heavy_brake) {
		amount = (float)((speed + fabs(motion->velocity[0])) * .3);
		sliding = 1;
	}

	if (control->gear == 0)
		sliding = 0;

	result->state.sliding = sliding;
	result->state.slide_amount = sliding ? amount : 0;
}

static void
scale_contact_limits(double grip_level, unsigned int contacts,
	struct contact_grip *grip, struct supported_grip_scales *output)
{
	double scales[GRIP_SCALES];
	volatile double value;

	grip_scales(grip_level, scales);
	output->reciprocal = 1.0 / contacts;
	value = output->reciprocal * scales[SLIP_SPEED_SCALE];
	grip->speed_limit = (float)(value * grip->speed_limit);
	value = output->reciprocal * scales[SLIP_ROTATION_SCALE];
	grip->rotation_limit = (float)(value * grip->rotation_limit);
	output->lateral = output->reciprocal * scales[LATERAL_FRICTION_SCALE];
	output->yaw = output->reciprocal * scales[YAW_FRICTION_SCALE];
}

static double
calculate_brake_factor(const struct mh_car *car,
	const struct mh_grip_control *control, double speed)
{
	double low_speed_brake, brake_loss, intercept;
	volatile double value;

	/* Retain the unrounded slope for the stored intercept. */
	value = BRAKE_FORCE_REFERENCE / (double)(float)car->brake_force;
	value -= 1;
	value /= LOW_SPEED_BRAKE_LIMIT;
	low_speed_brake = (float)value;
	value *= LOW_SPEED_BRAKE_LIMIT;
	value += 1;
	intercept = (float)value;
	value = speed * BRAKE_SPEED_LOSS_SCALE;
	value = 1 - value;
	value *= (double)(float)control->braking;
	brake_loss = value;

	if (speed < LOW_SPEED_BRAKE_LIMIT) {
		value = low_speed_brake * speed;
		value = intercept - value;
		value *= brake_loss;
		brake_loss = value;
	}

	/* Store loss before subtracting from one and clamping. */
	brake_loss = (float)brake_loss;

	return control->forced_stop ? 0 : stored_clamp(1 - brake_loss, 0, 1);
}

static void
apply_supported_friction(const struct mh_grip_control *control,
	const struct supported_grip_scales *scales, double brake_factor,
	struct contact_grip *grip)
{
	unsigned int i;

	for (i = 0; i < 2; i++) {
		grip->lateral[i] = (float)(grip->lateral[i] * scales->lateral);
		grip->longitudinal[i] =
			(float)(grip->longitudinal[i] * scales->reciprocal);

		if (control->body_contact)
			grip->longitudinal[i] = (float)(grip->longitudinal[i] *
				BODY_FRICTION_SCALE);

		grip->lateral[i] =
			cap_friction((float)(grip->lateral[i] * brake_factor));
		grip->longitudinal[i] = cap_friction(
			(float)(grip->longitudinal[i] * brake_factor));
		grip->yaw[i] =
			cap_friction((float)(grip->yaw[i] * scales->yaw));
	}
}

static void
calculate_supported_damping(const struct contact_grip *grip, int sliding,
	double step_seconds, struct mh_grip_result *result)
{
	/*
	 * The compatibility power function returns zero for negative or
	 * unordered bases with a fractional exponent. Valid step_seconds is
	 * (0,.1], hence always fractional. Near-vertical walls can have
	 * negative world Ny and therefore negative X friction. Reciprocal
	 * overflow and zero braking can also leave an unordered friction value;
	 * the same domain result applies.
	 */
	result->velocity_scale[0] = !(grip->lateral[sliding] >= 0)
		? 0
		: pow(grip->lateral[sliding], step_seconds);

	result->velocity_scale[2] =
		pow(grip->longitudinal[sliding], step_seconds);
	result->angular_scale[0] = result->angular_scale[2] =
		pow(TILT_FRICTION, step_seconds);
	result->angular_scale[1] = pow(grip->yaw[sliding], step_seconds);
}

static void
damp_supported(const struct mh_car *car, const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion, double grip_level,
	double step_seconds, struct contact_grip *grip,
	struct mh_grip_result *result)
{
	struct supported_grip_scales scales;
	double speed;

	scale_contact_limits(grip_level, result->contacts, grip, &scales);
	speed = fabs(motion->velocity[2]);
	result->brake_factor = calculate_brake_factor(car, control, speed);
	apply_supported_friction(control, &scales, result->brake_factor, grip);
	select_slide(state, control, motion, grip, speed, result);
	calculate_supported_damping(
		grip, result->state.sliding, step_seconds, result);
}

static enum mh_error_code
publish_tyre_effects(const struct mh_materials *materials,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control, struct mh_grip_result *result,
	struct mh_error *error_out)
{
	struct mh_tyre_effect_input effects;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		effects.supported[i] =
			result->state.wheels[i + FIRST_REAR_WHEEL].supported;
		effects.material[i] =
			result->state.wheels[i + FIRST_REAR_WHEEL].material;
	}

	effects.sliding = result->state.sliding;
	effects.slide = result->state.slide_amount;
	effects.wheelspin = control->wheelspin;
	effects.heavy_brake = control->heavy_brake;
	effects.rpm = control->rpm;
	effects.forward = result->velocity[2];

	return mh_tyre_effects_update(materials, &effects, &state->tyre_effects,
		&result->state.tyre_effects, error_out);
}

static void
apply_low_speed_hold(const struct mh_grip_control *control, double step_seconds,
	struct mh_grip_result *result)
{
	double hold_scale;

	if (fabs(result->velocity[2]) < HOLD_SPEED &&
		fabs((double)(float)control->longitudinal) < HOLD_FORCE) {
		hold_scale = control->heavy_brake
			? 0
			: pow(HOLD_FRICTION, step_seconds);
		result->velocity_scale[2] *= hold_scale;
		result->velocity[2] = control->heavy_brake
			? 0
			: (double)(float)(result->velocity[2] * hold_scale);
	}
}

enum mh_error_code
mh_grip_finish(const struct mh_car *car, const struct mh_materials *materials,
	const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion,
	const struct mh_suspension *contacts, double step_seconds,
	struct mh_grip_result *output, struct mh_error *error_out)
{
	return mh_grip_finish_tuned(car, materials, state, control, motion,
		contacts, car == NULL ? -1 : (double)car->grip_level,
		step_seconds, output, error_out);
}

enum mh_error_code
mh_grip_finish_tuned(const struct mh_car *car,
	const struct mh_materials *materials, const struct mh_grip_state *state,
	const struct mh_grip_control *control,
	const struct mh_motion_state *motion,
	const struct mh_suspension *contacts, double grip_level,
	double step_seconds, struct mh_grip_result *output,
	struct mh_error *error_out)
{
	struct mh_grip_result result;
	struct contact_grip grip;
	struct mh_motion_state stored_motion;
	enum mh_error_code status;
	unsigned int i;

	status = validate_inputs(
		materials, state, control, motion, step_seconds, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (car == NULL || contacts == NULL || output == NULL ||
		!car->has_grip ||
		!(car->brake_force >= BRAKE_FORCE_MINIMUM &&
			car->brake_force <= BRAKE_LIMIT) ||
		!(grip_level >= 0 && grip_level <= GRIP_LEVEL_LIMIT)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing or invalid car grip settings");
	}

	memset(&result, 0, sizeof(result));
	result.state = *state;
	result.state.rear_limit = 0;
	result.state.slide_amount = (float)state->slide_amount;
	result.brake_factor = 1;
	step_seconds = (float)step_seconds;
	stored_motion = *motion;

	for (i = 0; i < 3; i++) {
		stored_motion.velocity[i] = (float)motion->velocity[i];
		stored_motion.angular_velocity[i] =
			(float)motion->angular_velocity[i];
	}

	motion = &stored_motion;

	status = collect_contacts(
		materials, control, contacts, &grip, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++)
		result.velocity_scale[i] = result.angular_scale[i] = 1;

	if (result.contacts == 0)
		damp_airborne(control, step_seconds, &result);
	else
		damp_supported(car, state, control, motion, grip_level,
			step_seconds, &grip, &result);

	for (i = 0; i < 3; i++) {
		result.velocity[i] =
			(float)(motion->velocity[i] * result.velocity_scale[i]);
		result.angular_velocity[i] =
			(float)(motion->angular_velocity[i] *
				result.angular_scale[i]);
	}

	result.wheel_forward = result.velocity[2];

	apply_low_speed_hold(control, step_seconds, &result);

	status = publish_tyre_effects(
		materials, state, control, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
