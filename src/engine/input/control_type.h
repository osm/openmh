#ifndef MH_INPUT_CONTROL_TYPE_H
#define MH_INPUT_CONTROL_TYPE_H

/*
 * Control identity from the TurnLeft binding. These are identity metadata;
 * receiving them does not select a local device or change remote physics. Zero
 * is retained for unclassified/AI.
 */
enum mh_control_type {
	MH_CONTROL_UNSPECIFIED = 0,
	MH_CONTROL_KEYBOARD = 1,
	MH_CONTROL_MOUSE = 2,
	MH_CONTROL_JOYSTICK = 3
};

/*
 * Classify applied TurnLeft tags in keyboard, mouse, joystick order; later tags
 * override earlier ones. An untagged binding retains the previous identity
 * word. This precedence differs from binding activation.
 */
unsigned long	mh_control_type_update(unsigned long left_binding,
	unsigned long previous);

#endif
