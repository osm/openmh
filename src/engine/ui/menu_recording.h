#ifndef MH_MENU_RECORDING_H
#define MH_MENU_RECORDING_H

#include "engine/core/error.h"
#include "engine/input/text_edit.h"
#include "engine/ui/menu.h"

#define MH_MENU_RECORDING_NAME_BYTES 128
#define MH_MENU_RECORDING_EVENTS 784

/*
 * Recording states: 2 prompt, 3 wait, 4 answer, 5 name, 6 overwrite prompt, 7
 * wait, 8 answer. 0 done, 1 launcher-pending.
 */
enum mh_menu_recording_phase {
	MH_MENU_RECORDING_DONE = 0,
	MH_MENU_RECORDING_LAUNCH_PENDING = 1,
	MH_MENU_RECORDING_SAVE_PROMPT = 2,
	MH_MENU_RECORDING_SAVE_WAIT = 3,
	MH_MENU_RECORDING_SAVE_ANSWER = 4,
	MH_MENU_RECORDING_NAME = 5,
	MH_MENU_RECORDING_OVERWRITE_PROMPT = 6,
	MH_MENU_RECORDING_OVERWRITE_WAIT = 7,
	MH_MENU_RECORDING_OVERWRITE_ANSWER = 8
};

struct mh_menu_recording {
	unsigned int	phase, pending_probe;
	unsigned long	answer;

	/* Retained between takes and across Cancel. */
	unsigned char	name[MH_MENU_RECORDING_NAME_BYTES];
};

enum mh_menu_recording_event {
	MH_MENU_RECORDING_PROBE = 5,
	MH_MENU_RECORDING_PUBLISH,
	MH_MENU_RECORDING_RETURN,
	MH_MENU_RECORDING_REFRESH
};

struct mh_menu_recording_input {
	unsigned int	modal_visible;
	unsigned long	clock_ms;
	unsigned char	keys[MH_KEY_COUNT], characters[MH_KEY_COUNT];
};

struct mh_menu_recording_output {
	struct mh_menu_event	events[MH_MENU_RECORDING_EVENTS];
	unsigned int		count, draw_name, cursor;
	unsigned long		cursor_width;

	/* Name is drawn BEFORE this frame's edits. */
	unsigned char		display_name[MH_MENU_RECORDING_NAME_BYTES];
};

/*
 * Caller retains the previous page, switches to layout 24 and begins this
 * controller. Initial zero state has an empty name; begin retains it.
 */
enum mh_error_code	mh_menu_recording_begin(struct mh_menu_recording *state,
	struct mh_error *error_out);

/*
 * Enter precedes Escape, then the original byte editor. Dialogs use
 * initial=cancel=1. PROBE requests a destination existence check; process
 * events in order, then resume with finish_probe. PUBLISH value 1 confirms
 * overwrite, 0 requests a new name; RETURN restores the retained page. REFRESH
 * follows publication. No filesystem or SDL types. Atomic errors.
 */
enum mh_error_code	mh_menu_recording_update(
	struct mh_menu_recording *state,
	const struct mh_menu_recording_input *input,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_menu_recording_output *output, struct mh_error *error_out);
enum mh_error_code	mh_menu_recording_finish_probe(
	struct mh_menu_recording *state, int exists,
	struct mh_menu_recording_output *output, struct mh_error *error_out);

#endif
