#ifndef MH_SETTINGS_CONTROL_LAYOUT_INTERNAL_H
#define MH_SETTINGS_CONTROL_LAYOUT_INTERNAL_H

#include "engine/core/error.h"
#include "engine/settings/control_layout.h"

/* Original action order, shared by the CLO reader and writer. */
extern const char *const mh_control_action_names[MH_CONTROL_FILE_BINDINGS];
extern const char *const mh_control_mouse_keys[MH_CONTROL_MOUSE_AXES];

/* Checks representation only; printable comment punctuation is allowed. */
enum mh_error_code	mh_control_layout_validate(
	const struct mh_control_layout *layout, struct mh_error *error_out);

#endif
