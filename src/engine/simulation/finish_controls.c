#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/finish_controls.h"

#define AI_ROLE 2
#define DISPLAY_LIMIT 1e6
#define GOAL_WHEEL_LIMIT 1e12
#define FINISH_THROTTLE .05f
#define FINISH_BRAKE .1f

enum mh_error_code
mh_finish_controls(int active, int finished, unsigned long role,
	float display_z, float goal_wheel, struct mh_engine_input *controls,
	struct mh_error *error_out)
{
	if (controls == NULL || (active != 0 && active != 1) ||
		(finished != 0 && finished != 1) || role > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish control state");

	if (!active || !finished)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!(display_z >= -DISPLAY_LIMIT && display_z <= DISPLAY_LIMIT) ||
		(role == AI_ROLE &&
			!(goal_wheel >= -GOAL_WHEEL_LIMIT &&
				goal_wheel <= GOAL_WHEEL_LIMIT)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish control coordinates");

	controls->throttle = display_z > 0 ? 0 : (double)FINISH_THROTTLE;
	controls->brake = display_z > 0 ? (double)FINISH_BRAKE : 0;

	if (role == AI_ROLE) {
		controls->steering = goal_wheel;
		controls->shift_up = controls->shift_down =
			controls->handbrake = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
