#include <float.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/input/drive_controls.h"

static int
finite_float(float value)
{
	return value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

static enum mh_error_code
store_magnitude(long double value, float *output, struct mh_error *error_out)
{
	if (value != value || value < -(long double)FLT_MAX ||
		value > (long double)FLT_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"driving control magnitude is not finite");

	*output = (float)value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
store_pedal(long double value, float *output, struct mh_error *error_out)
{
	/*
	 * A positive overflow would be clamped to one after the original float
	 * store. Compare first to avoid overflowing the native conversion.
	 */
	return store_magnitude(value > 1 ? 1 : value, output, error_out);
}

static enum mh_error_code
shape_brake(const struct mh_drive_controls *previous, float seconds,
	struct mh_drive_controls *controls, struct mh_error *error_out)
{
	long double value;

	controls->command = MH_DRIVE_COMMAND_BRAKE;

	if (controls->brake < 1)
		value = (long double)controls->brake * (long double)1.1;
	else if (previous->brake > .699)
		value = (long double)previous->brake +
			(long double)seconds * (long double).5;
	else
		value = (long double).7f;

	return store_pedal(value, &controls->brake, error_out);
}

static enum mh_error_code
steer_left(const struct mh_control_binding_set *bindings,
	const struct mh_drive_controls *previous, float seconds,
	struct mh_drive_controls *controls, struct mh_error *error_out)
{
	long double value;

	value = -(long double)controls->steering;

	if (bindings->codes[MH_CONTROL_LEFT] & MH_BINDING_MOUSE) {
		if (value < -1)
			value = -1;
	} else {
		controls->command = MH_DRIVE_COMMAND_LEFT;

		if (controls->steering >= 1) {
			value = previous->steering < -.49
				? (long double)previous->steering -
					(long double)seconds * (long double).6
				: -.5L;

			if (value < -1)
				value = -1;
		}
	}

	return store_magnitude(value, &controls->steering, error_out);
}

static enum mh_error_code
steer_right(const struct mh_control_binding_set *bindings,
	const struct mh_drive_controls *previous, float seconds,
	struct mh_drive_controls *controls, struct mh_error *error_out)
{
	long double value;

	value = controls->steering;

	/*
	 * Both original direction branches inspect TurnLeft's mouse
	 * tag.
	 */
	if (bindings->codes[MH_CONTROL_LEFT] & MH_BINDING_MOUSE) {
		if (value > 1)
			value = 1;
	} else {
		controls->command = MH_DRIVE_COMMAND_RIGHT;

		if (controls->steering >= 1) {
			value = previous->steering > .49
				? (long double)previous->steering +
					(long double)seconds * (long double).6
				: .5L;

			if (value > 1)
				value = 1;
		}
	}

	return store_magnitude(value, &controls->steering, error_out);
}

enum mh_error_code
mh_drive_controls_step(const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input,
	const struct mh_drive_controls *previous, float seconds,
	int race_started, int menu_visible, int chat,
	struct mh_drive_controls *output, struct mh_error *error_out)
{
	struct mh_drive_controls next;
	unsigned int active;
	enum mh_error_code status;

	if (bindings == NULL || input == NULL || previous == NULL ||
		output == NULL || !finite_float(previous->brake) ||
		!finite_float(previous->steering) ||
		!(seconds >= 0 && seconds <= 1) ||
		(race_started != 0 && race_started != 1) ||
		(menu_visible != 0 && menu_visible != 1) ||
		(chat != 0 && chat != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving control frame");

	memset(&next, 0, sizeof(next));

	if (chat) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (race_started && !menu_visible) {
		status = mh_binding_query(input,
			bindings->codes[MH_CONTROL_BRAKE], 0, &active,
			&next.brake, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (active) {
			status = shape_brake(
				previous, seconds, &next, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_ACCELERATE],
		0, &active, &next.throttle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active) {
		next.command = MH_DRIVE_COMMAND_ACCELERATE;
		status = store_pedal(
			(long double)next.throttle * (long double)1.1,
			&next.throttle, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_LEFT], 0,
		&active, &next.steering, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active) {
		status = steer_left(
			bindings, previous, seconds, &next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_RIGHT], 0,
		&active, &next.steering, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active) {
		status = steer_right(
			bindings, previous, seconds, &next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_SHIFT_UP],
		0, &active, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active)
		next.command = MH_DRIVE_COMMAND_SHIFT_UP;

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_SHIFT_DOWN],
		0, &active, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active)
		next.command = MH_DRIVE_COMMAND_SHIFT_DOWN;

	status = mh_binding_query(input, bindings->codes[MH_CONTROL_HANDBRAKE],
		0, &next.handbrake, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
