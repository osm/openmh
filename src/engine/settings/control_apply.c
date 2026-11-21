#include <string.h>

#include "engine/core/error.h"
#include "engine/input/binding.h"
#include "engine/settings/control_layout_internal.h"

/* Digit shortcuts bypass this conversion in the caller. */
static unsigned long
binding_word(unsigned long code)
{
	if (code < MH_KEY_MOUSE_IN)
		return MH_BINDING_KEYBOARD | code;

	if (code <= MH_KEY_MOUSE_OUT)
		return MH_BINDING_MOUSE |
			(MH_BINDING_DIRECTION_FIRST
				<< (4 + code - MH_KEY_MOUSE_IN));

	if (code <= MH_KEY_JOYSTICK_BUTTON_1)
		return MH_BINDING_JOYSTICK |
			(1UL << (MH_BINDING_BUTTON_SHIFT +
				MH_KEY_JOYSTICK_BUTTON_1 - code));

	if (code <= MH_KEY_JOYSTICK_OUT)
		return MH_BINDING_JOYSTICK |
			(MH_BINDING_DIRECTION_FIRST
				<< (code - MH_KEY_JOYSTICK_LEFT));

	if (code <= MH_KEY_MOUSE_DOWN)
		return MH_BINDING_MOUSE |
			(MH_BINDING_DIRECTION_FIRST
				<< (code - MH_KEY_MOUSE_LEFT));

	if (code == MH_KEY_MOUSE_BUTTON_LEFT)
		return MH_BINDING_MOUSE | MH_BINDING_MOUSE_BUTTON_LEFT;

	if (code == MH_KEY_MOUSE_BUTTON_RIGHT)
		return MH_BINDING_MOUSE | MH_BINDING_MOUSE_BUTTON_RIGHT;

	if (code == MH_KEY_MOUSE_BUTTON_MIDDLE)
		return MH_BINDING_MOUSE | MH_BINDING_MOUSE_BUTTON_MIDDLE;

	return 0;
}

enum mh_error_code
mh_control_layout_apply(const struct mh_control_layout *layout,
	struct mh_control_binding_set *output, struct mh_error *error_out)
{
	struct mh_control_binding_set next;
	enum mh_error_code status;
	unsigned int i;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing applied control output");

	status = mh_control_layout_validate(layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	memcpy(next.mouse_speed, layout->mouse_speed, sizeof(next.mouse_speed));
	next.force_feedback = layout->force_feedback;

	for (i = 0; i < MH_CONTROL_BINDINGS; i++) {
		if (i < MH_CONTROL_FILE_BINDINGS)
			next.codes[i] = binding_word(layout->bindings[i]);
		else
			next.codes[i] = layout->bindings[i];
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
