#include <float.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/binding.h"

static int
coordinate_valid(long value)
{
	return value >= MH_I32_MIN && value <= MH_I32_MAX;
}

static enum mh_error_code
activate(long double value, unsigned int *active, float *amount,
	struct mh_error *error_out)
{
	if (value != value || value < -(long double)FLT_MAX ||
		value > (long double)FLT_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"control binding magnitude is not finite");

	if (amount != NULL)
		*amount = (float)value;

	*active = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_binding_query(const struct mh_binding_input *input, unsigned long binding,
	int repeat, unsigned int *active, float *amount,
	struct mh_error *error_out)
{
	unsigned int axis, direction, scan;
	float center, minimum, position;
	long double value, denominator, lower;

	if (input == NULL || active == NULL || binding > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control binding query");

	for (axis = 0; axis < MH_BINDING_AXES; axis++)
		if (!coordinate_valid(input->mouse[axis]) ||
			!coordinate_valid(input->joystick[axis]) ||
			!coordinate_valid(input->minimum[axis]) ||
			!coordinate_valid(input->maximum[axis]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"control device coordinate exceeds signed word");

	if (binding & MH_BINDING_MOUSE) {
		/* Original button order: left, middle, right. */
		if (((input->mouse_buttons & MH_MOUSE_BUTTON_LEFT) &&
			(binding & MH_BINDING_MOUSE_BUTTON_LEFT)) ||
			((input->mouse_buttons & MH_MOUSE_BUTTON_MIDDLE) &&
				(binding & MH_BINDING_MOUSE_BUTTON_MIDDLE)) ||
			((input->mouse_buttons & MH_MOUSE_BUTTON_RIGHT) &&
				(binding & MH_BINDING_MOUSE_BUTTON_RIGHT)))
			return activate(1, active, amount, error_out);

		for (axis = 0; axis < MH_BINDING_AXES; axis++) {
			if (input->mouse[axis] == 0)
				continue;

			direction = axis * 2 + (input->mouse[axis] < 0 ? 0 : 1);

			/* Test negative Y's bit for positive Z. */
			if (axis == 2 && input->mouse[axis] > 0)
				direction = 2;

			if (!(binding &
				(MH_BINDING_DIRECTION_FIRST << direction)))
				continue;

			if (amount == NULL) {
				*active = 1;
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}

			if (input->mouse_speed[axis] !=
					input->mouse_speed[axis] ||
				input->mouse_speed[axis] < -FLT_MAX ||
				input->mouse_speed[axis] > FLT_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"control mouse sensitivity is not finite");

			value = (long double)input->mouse[axis] *
				input->mouse_speed[axis];

			if (input->mouse[axis] < 0)
				value = -value;

			/* Binary64 1/30, promoted without changing its bits. */
			value *= (long double)0.033333333333333333;

			if (value > 1)
				value = 1;

			return activate(value, active, amount, error_out);
		}
	}

	if (binding & MH_BINDING_JOYSTICK) {
		if ((binding >> MH_BINDING_BUTTON_SHIFT) &
			input->joystick_buttons &
			MH_BINDING_JOYSTICK_BUTTON_MASK)
			return activate(1, active, amount, error_out);

		for (axis = 0; axis < MH_BINDING_AXES; axis++) {
			/*
			 * The original rounds X minimum before computing its
			 * centre; Y/Z minima remain the exact signed integer at
			 * that point.
			 */
			minimum = (float)input->minimum[axis];
			lower = axis == 0 ? (long double)minimum
					: (long double)input->minimum[axis];
			center = (float)(((long double)input->maximum[axis] +
						lower) *
				.5L);

			if ((long double)input->joystick[axis] ==
				(long double)center)
				continue;

			direction = axis * 2 +
				((long double)input->joystick[axis] <
							(long double)center
						? 0
						: 1);

			if (!(binding &
				(MH_BINDING_DIRECTION_FIRST << direction)))
				continue;

			if (amount == NULL) {
				*active = 1;
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}

			position = (float)input->joystick[axis];
			denominator = direction & 1
				? (long double)input->maximum[axis]
				: lower;
			denominator -= (long double)center;

			if (denominator == 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"singular control joystick calibration");

			value = ((long double)position - (long double)center) /
				denominator;

			/*
			 * Original compares the extended ratio to binary64
			 * 0.98, not the rounded output float. There is no lower
			 * clamp.
			 */
			if (value > (long double)0.98)
				value = 1;

			return activate(value, active, amount, error_out);
		}
	}

	if (binding & MH_BINDING_KEYBOARD) {
		scan = (unsigned int)(binding & MH_BINDING_SCAN_MASK);

		if (repeat ? input->events[scan] != 0
			: (input->down[scan] & MH_KEY_DOWN_BIT) != 0)
			return activate(1, active, amount, error_out);
	}

	*active = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
