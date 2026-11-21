#include <float.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/view_controls.h"

#define OPENING_DELAY_MS 6000UL

struct view_binding {
	enum mh_control_action	control;
	enum mh_view_request	request;
};

enum mh_error_code
mh_view_controls_query(const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input, double clock_ms,
	unsigned long start_clock, int chat_active,
	unsigned int previous_look_back, struct mh_view_controls *output,
	struct mh_error *error_out)
{
	static const struct view_binding views[] = {
		{MH_CONTROL_IN_CAR, MH_VIEW_IN_CAR},
		{MH_CONTROL_OUT_CAR, MH_VIEW_OUT_CAR},
		{MH_CONTROL_CAMERA, MH_VIEW_TRACK},
	};

	struct mh_view_controls next;
	unsigned int i, active;
	enum mh_error_code status;

	if (bindings == NULL || input == NULL || output == NULL ||
		!(clock_ms >= -DBL_MAX && clock_ms <= DBL_MAX) ||
		start_clock > MH_U32_MAX ||
		(chat_active != 0 && chat_active != 1) ||
		previous_look_back > 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid camera control frame");

	next.requests = 0;
	next.look_back = previous_look_back;

	if (!chat_active &&
		(clock_ms < (double)start_clock ||
			clock_ms > (double)((start_clock + OPENING_DELAY_MS) &
					MH_U32_MAX))) {
		for (i = 0; i < sizeof(views) / sizeof(views[0]); i++) {
			status = mh_binding_query(input,
				bindings->codes[views[i].control], 0, &active,
				NULL, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (active)
				next.requests |= views[i].request;
		}

		status = mh_binding_query(input,
			bindings->codes[MH_CONTROL_REAR_VIEW], 0,
			&next.look_back, NULL, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_binding_query(input,
			bindings->codes[MH_CONTROL_CYCLE], 1, &active, NULL,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (active)
			next.requests |= MH_VIEW_CYCLE;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
