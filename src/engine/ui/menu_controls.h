#ifndef MH_MENU_CONTROLS_H
#define MH_MENU_CONTROLS_H

#include "engine/core/error.h"
#include "engine/settings/control_layout.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_choices.h"

#define MH_MENU_CONTROL_ACTIONS 10
#define MH_MENU_CONTROL_CAPACITY MH_CONTROL_PROFILES
#define MH_MENU_CONTROL_EVENTS 16

enum mh_menu_controls_column {
	MH_MENU_CONTROLS_PROFILES = 0,
	MH_MENU_CONTROLS_BINDINGS
};

enum mh_menu_controls_row {
	MH_MENU_CONTROLS_FEEDBACK = 0,
	MH_MENU_CONTROLS_MOUSE_X,
	MH_MENU_CONTROLS_MOUSE_Y,
	MH_MENU_CONTROLS_MOUSE_Z,
	MH_MENU_CONTROLS_FIRST_ACTION,
	MH_MENU_CONTROLS_ROWS =
		MH_MENU_CONTROLS_FIRST_ACTION + MH_MENU_CONTROL_ACTIONS
};

enum mh_menu_controls_phase {
	MH_MENU_CONTROLS_IDLE = 0,
	MH_MENU_CONTROLS_START = 1,
	MH_MENU_CONTROLS_CAPTURE = 2,
	MH_MENU_CONTROLS_FINISH = 3,
	MH_MENU_CONTROLS_RESTORE = 9,
	MH_MENU_CONTROLS_RELEASE = 10
};

enum mh_menu_controls_event {
	MH_MENU_CONTROLS_INPUT_MODE = 5
};

struct mh_menu_controls_state {
	unsigned int	column, row, selected, phase;
	unsigned int	held_key, held_count;
	float		capture_started;

	/*
	 * Patched artwork adds four rows; unavailable is a preference-row mask.
	 */
	unsigned int	preferences, unavailable;
};

struct mh_menu_controls_input {
	unsigned int	left, right, up, down, enter, escape;

	/*
	 * One poll's legacy held-key list; only its first byte is consumed.
	 * The original keyboard producer sorts the list by scan index.
	 */
	unsigned int	held_count, first_held;
	unsigned long	clock_ms;
	unsigned int	held_left, held_right, shift, control, alt;
	unsigned long	elapsed_ms;
};

struct mh_menu_controls_output {
	struct mh_menu_event	events[MH_MENU_CONTROL_EVENTS];
	unsigned int		count;
};

/*
 * Control editing, retaining baseline navigation for legacy artwork. Column 0
 * selects a profile; column 1 edits its first ten actions. Bindings 10..27
 * reserve keys. All other editable rows sharing a new binding receive the
 * selected row's old binding, in order. Events preserve sounds, page requests
 * and input-mode writes. Mode 0 disables synthesized joystick navigation during
 * capture; it is not a keyboard reset. Adapters process every event, including
 * those after an exit request.
 *
 * Capture waits for release, with an eight-second timeout. After assignment, a
 * changed first held key or key count schedules input restoration on the next
 * poll. The clock anchor retains the original float store.
 *
 * Patched rows precede the actions with feedback and X/Y/Z speed. Held arrows
 * adjust speeds before navigation, from .01 to 10. Unavailable preferences
 * remain visible but cannot change; this is native host policy. Page
 * initialization retains this state. Bounds are native policy; errors leave
 * state, every profile and output unchanged.
 */
enum mh_error_code	mh_menu_controls_update(
	struct mh_menu_controls_state *state,
	struct mh_control_profile *profiles, unsigned int count,
	const struct mh_menu_controls_input *input,
	struct mh_menu_controls_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

#endif
