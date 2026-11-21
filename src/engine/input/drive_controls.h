#ifndef MH_INPUT_DRIVE_CONTROLS_H
#define MH_INPUT_DRIVE_CONTROLS_H

#include "engine/core/error.h"
#include "engine/input/binding.h"
#include "engine/settings/control_layout.h"

/* Assigned in query order, despite their single-bit encoding. */
enum mh_drive_command {
	MH_DRIVE_COMMAND_ACCELERATE = 1,
	MH_DRIVE_COMMAND_BRAKE = 2,
	MH_DRIVE_COMMAND_RIGHT = 4,
	MH_DRIVE_COMMAND_LEFT = 8,
	MH_DRIVE_COMMAND_SHIFT_UP = 0x10,
	MH_DRIVE_COMMAND_SHIFT_DOWN = 0x20
};

/*
 * Local player's controls, including pedal/steering branches. Command is
 * assigned in query order, not ORed.
 */
struct mh_drive_controls {
	unsigned int	command, handbrake;
	float		throttle, brake, steering;
};

/*
 * Previous brake/steering are sampled before clearing this frame's controls.
 * seconds is the already-selected frame interval, in [0,1]. Original brake
 * queries require race_started && !menu_visible; chat suppresses this whole
 * block. Other native UI/focus suppression belongs to the input backend. Output
 * may alias previous; errors preserve it.
 */
enum mh_error_code	mh_drive_controls_step(
	const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input,
	const struct mh_drive_controls *previous, float seconds,
	int race_started, int menu_visible, int chat,
	struct mh_drive_controls *output, struct mh_error *error_out);

#endif
