#ifndef MH_MENU_MODAL_H
#define MH_MENU_MODAL_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"

/* Left/Right and both exit keys can emit sounds in the same step. */
#define MH_MENU_MODAL_SOUNDS 6

struct mh_menu_modal_state {
	float		x, direction;
	unsigned int	visible, selected, cancel, choices;
	unsigned long	answer; /* One-based answer, zero while unanswered. */
};

struct mh_menu_modal_output {
	float		display_x;
	unsigned int	display_selected, sounds[MH_MENU_MODAL_SOUNDS], count;
};

/*
 * Modal initialization and animation/input. Choices and initial/cancel
 * arguments are one-based; zero also selects the first choice. Text
 * copying/layout is separate. After successful open, the adapter requests
 * PAGE_IN sound 2. Step accepts an active modal; drawing uses the returned
 * pre-update snapshot. Enter then Escape on the same frame leaves the cancel
 * answer. The adapter keeps page input blocked until visible becomes zero, even
 * with an answer. Errors preserve state/output. Text adapter must bound choices
 * <=256.
 */
enum mh_error_code	mh_menu_modal_open(struct mh_menu_modal_state *state,
	unsigned int choices, unsigned int initial, unsigned int cancel,
	struct mh_error *error_out);
enum mh_error_code	mh_menu_modal_step(struct mh_menu_modal_state *state,
	unsigned long elapsed_ms, const struct mh_menu_input *input,
	struct mh_menu_modal_output *output, struct mh_error *error_out);

#endif
