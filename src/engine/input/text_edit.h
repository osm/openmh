#ifndef MH_INPUT_TEXT_EDIT_H
#define MH_INPUT_TEXT_EDIT_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/input/keyboard.h"

#define MH_TEXT_EDIT_BYTES 256
#define MH_TEXT_EDIT_CHARACTERS 256
/* One action per key, one extra Backspace, and all possible removals. */
#define MH_TEXT_EDIT_ACTIONS 768

enum mh_text_edit_action {
	MH_TEXT_EDIT_ACCEPT = 1,
	MH_TEXT_EDIT_REJECT
};

struct mh_text_edit_output {
	unsigned long last_key,
		width; /* last_key=0xffffffff when no event fired. */
	unsigned int	count;
	unsigned char	actions[MH_TEXT_EDIT_ACTIONS];
};

/*
 * Ascending scan order, byte-character append and Backspace, then remove
 * trailing bytes until proportional width fits. limit includes NUL; capacity
 * bounds the actual buffer (both <=256). Preexisting text may exceed limit.
 * Only key event 1 acts. Character translation is platform-owned. Actions
 * preserve every original accept/reject callback, including an accepted
 * insertion subsequently removed by the width limit. Atomic errors.
 */
enum mh_error_code	mh_text_edit(unsigned char *text, size_t capacity,
	unsigned int limit, unsigned long max_width,
	const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_text_edit_output *output, struct mh_error *error_out);

#endif
