#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/devices.h"

#define MOUSE_SAMPLE_MS 100UL
#define MOUSE_DIRECTION_THRESHOLD 25
#define JOYSTICK_THRESHOLD_NUMERATOR 5UL
#define JOYSTICK_THRESHOLD_DENOMINATOR 6UL

static int
coordinate_valid(long value)
{
	return value >= MH_I32_MIN && value <= MH_I32_MAX;
}

static long
signed_i32(unsigned long value)
{
	value &= MH_U32_MAX;

	if (value <= (unsigned long)MH_I32_MAX)
		return (long)value;

	return -1L - (long)(MH_U32_MAX - value);
}

static long
add_i32(long a, long b)
{
	return signed_i32((unsigned long)a + (unsigned long)b);
}

static long
subtract_i32(long a, long b)
{
	return signed_i32((unsigned long)a - (unsigned long)b);
}

/* C89 leaves rounding of negative integer division implementation-defined. */
static long
truncate_divide(long value, unsigned long divisor)
{
	if (value >= 0)
		return (long)((unsigned long)value / divisor);

	return -(long)(((unsigned long)(-(value + 1L)) + 1UL) / divisor);
}

static long
floor_half(long value)
{
	if (value >= 0)
		return value / 2;

	return -1L - ((-1L - value) / 2);
}

enum mh_error_code
mh_mouse_filter_step(long previous[MH_DEVICE_AXES],
	const long raw[MH_DEVICE_AXES], long output[MH_DEVICE_AXES],
	struct mh_error *error_out)
{
	long current[MH_DEVICE_AXES], filtered[MH_DEVICE_AXES];
	unsigned int i;

	if (previous == NULL || raw == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing mouse filter sample");

	for (i = 0; i < MH_DEVICE_AXES; i++) {
		if (!coordinate_valid(previous[i]) || !coordinate_valid(raw[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid mouse filter sample");

		current[i] = raw[i];
		filtered[i] =
			truncate_divide(add_i32(previous[i], current[i]), 2);
	}

	memcpy(previous, current, sizeof(current));
	memcpy(output, filtered, sizeof(filtered));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Patched capture accepts motion beyond five sixths of each half-range. */
static long
joystick_threshold(long center, long endpoint)
{
	long distance;

	distance = signed_i32((unsigned long)subtract_i32(endpoint, center) *
		JOYSTICK_THRESHOLD_NUMERATOR);

	return add_i32(center,
		truncate_divide(distance, JOYSTICK_THRESHOLD_DENOMINATOR));
}

static void
synthesize_joystick(const struct mh_device_input *input, unsigned long mode,
	unsigned char keys[MH_KEY_COUNT])
{
	unsigned int i, count;
	long center, lower, upper;

	for (i = 0; i < MH_DEVICE_AXES && i < input->joystick_axis_count; i++) {
		center = floor_half(
			add_i32(input->minimum[i], input->maximum[i]));
		lower = joystick_threshold(center, input->minimum[i]);
		upper = joystick_threshold(center, input->maximum[i]);

		if (input->joystick[i] < lower)
			keys[MH_KEY_JOYSTICK_LEFT + 2 * i] = MH_KEY_DOWN_BIT;

		if (input->joystick[i] > upper)
			keys[MH_KEY_JOYSTICK_RIGHT + 2 * i] = MH_KEY_DOWN_BIT;
	}

	count = input->joystick_button_count > MH_JOYSTICK_BUTTONS
		? MH_JOYSTICK_BUTTONS
		: (unsigned int)input->joystick_button_count;

	for (i = 0; i < count; i++)
		if (input->joystick_buttons[i] == 1)
			keys[MH_KEY_JOYSTICK_BUTTON_1 - i] = MH_KEY_DOWN_BIT;

	if (mode == MH_DEVICE_KEYS_MENU) {
		keys[MH_KEY_LEFT] |= keys[MH_KEY_JOYSTICK_LEFT];
		keys[MH_KEY_RIGHT] |= keys[MH_KEY_JOYSTICK_RIGHT];
		keys[MH_KEY_UP] |= keys[MH_KEY_JOYSTICK_UP];
		keys[MH_KEY_DOWN] |= keys[MH_KEY_JOYSTICK_DOWN];
		keys[MH_KEY_ENTER] |= keys[MH_KEY_JOYSTICK_BUTTON_1];
		keys[MH_KEY_ESCAPE] |= keys[MH_KEY_JOYSTICK_BUTTON_2];
	}
}

static void
synthesize_mouse(struct mh_device_keys *state,
	const struct mh_device_input *input, unsigned long mode,
	unsigned char keys[MH_KEY_COUNT])
{
	unsigned int i, key;

	for (i = 0; i < MH_DEVICE_AXES; i++) {
		state->mouse[i] = add_i32(state->mouse[i], input->mouse[i]);

		if (i >= input->mouse_axis_count)
			continue;

		key = i == 2 ? MH_KEY_MOUSE_IN : MH_KEY_MOUSE_LEFT + 2 * i;

		if (state->mouse[i] < -MOUSE_DIRECTION_THRESHOLD)
			keys[key] = MH_KEY_DOWN_BIT;

		if (state->mouse[i] > MOUSE_DIRECTION_THRESHOLD)
			keys[key + 1] = MH_KEY_DOWN_BIT;
	}

	for (i = 0; i < MH_MOUSE_BUTTONS; i++)
		if (input->mouse_buttons[i] == 1)
			keys[MH_KEY_MOUSE_BUTTON_LEFT + i] = MH_KEY_DOWN_BIT;

	if (mode == MH_DEVICE_KEYS_MENU) {
		keys[MH_KEY_LEFT] |= keys[MH_KEY_MOUSE_LEFT];
		keys[MH_KEY_RIGHT] |= keys[MH_KEY_MOUSE_RIGHT];
		keys[MH_KEY_UP] |= keys[MH_KEY_MOUSE_UP];
		keys[MH_KEY_DOWN] |= keys[MH_KEY_MOUSE_DOWN];
		keys[MH_KEY_ENTER] |= keys[MH_KEY_MOUSE_BUTTON_LEFT];
		keys[MH_KEY_ESCAPE] |= keys[MH_KEY_MOUSE_BUTTON_RIGHT];
	}
}

enum mh_error_code
mh_device_keys_poll(struct mh_device_keys *state,
	const struct mh_device_input *input,
	const unsigned char down[MH_KEY_COUNT], unsigned long clock,
	int reset_clock, unsigned long mode, unsigned char output[MH_KEY_COUNT],
	struct mh_error *error_out)
{
	struct mh_device_keys next;
	unsigned char keys[MH_KEY_COUNT];
	unsigned int i;

	if (state == NULL || input == NULL || down == NULL || output == NULL ||
		clock > MH_U32_MAX || mode > MH_U32_MAX ||
		(reset_clock != 0 && reset_clock != 1) ||
		state->sample_clock > MH_U32_MAX ||
		input->mouse_flags > MH_U32_MAX ||
		input->joystick_flags > MH_U32_MAX ||
		input->joystick_button_count > MH_U32_MAX ||
		input->mouse_axis_count > MH_U32_MAX ||
		input->joystick_axis_count > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid device key poll");

	for (i = 0; i < MH_DEVICE_AXES; i++)
		if (!coordinate_valid(state->mouse[i]) ||
			!coordinate_valid(input->mouse[i]) ||
			!coordinate_valid(input->joystick[i]) ||
			!coordinate_valid(input->minimum[i]) ||
			!coordinate_valid(input->maximum[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid device coordinate");

	for (i = 0; i < MH_MOUSE_BUTTONS; i++)
		if (input->mouse_buttons[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid mouse button");

	for (i = 0; i < MH_JOYSTICK_BUTTONS; i++)
		if (input->joystick_buttons[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid joystick button");

	next = *state;
	memcpy(keys, down, sizeof(keys));

	if (reset_clock)
		next.sample_clock = 0;

	if (((clock - next.sample_clock) & MH_U32_MAX) > MOUSE_SAMPLE_MS) {
		next.sample_clock = clock;
		memset(next.mouse, 0, sizeof(next.mouse));
	}

	if ((input->joystick_flags & MH_DEVICE_PRESENT) != 0)
		synthesize_joystick(input, mode, keys);

	if ((input->mouse_flags & MH_DEVICE_PRESENT) != 0)
		synthesize_mouse(&next, input, mode, keys);

	*state = next;
	memcpy(output, keys, sizeof(keys));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
