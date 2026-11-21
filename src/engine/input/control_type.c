#include "engine/input/binding.h"
#include "engine/input/control_type.h"

unsigned long
mh_control_type_update(unsigned long left_binding, unsigned long previous)
{
	if (left_binding & MH_BINDING_KEYBOARD)
		previous = MH_CONTROL_KEYBOARD;

	if (left_binding & MH_BINDING_MOUSE)
		previous = MH_CONTROL_MOUSE;

	if (left_binding & MH_BINDING_JOYSTICK)
		previous = MH_CONTROL_JOYSTICK;

	return previous;
}
