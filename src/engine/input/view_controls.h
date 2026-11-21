#ifndef MH_INPUT_VIEW_CONTROLS_H
#define MH_INPUT_VIEW_CONTROLS_H

#include "engine/core/error.h"
#include "engine/input/binding.h"
#include "engine/settings/control_layout.h"

enum mh_view_request {
	MH_VIEW_IN_CAR = 1,
	MH_VIEW_OUT_CAR = 2,
	MH_VIEW_TRACK = 4,
	MH_VIEW_CYCLE = 8
};

struct mh_view_controls {
	unsigned int	requests, look_back;
};

/*
 * View controls with the preceding chat gate. The three views and rear view
 * query held state; cycling queries repeat events. Apply requests in IN_CAR,
 * OUT_CAR, TRACK, CYCLE order. All three view requests can occur in one frame.
 * The old rear-view value survives the chat gate and the inclusive
 * [start_clock, wrap(start_clock+6000)] opening interval. Clock comparison uses
 * two comparisons, even if the upper endpoint wrapped. Gated frames do not
 * query bindings. A failed query preserves output. Camera selection and cycling
 * belong to the caller.
 */
enum mh_error_code	mh_view_controls_query(
	const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input, double clock_ms,
	unsigned long start_clock, int chat_active,
	unsigned int previous_look_back, struct mh_view_controls *output,
	struct mh_error *error_out);

#endif
