#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/recovery.h"

#define TIMER_LIMIT 1e9
#define SPEED_LIMIT 1e4
#define POSITION_LIMIT 1e6
#define STEP_LIMIT .5f
#define FALL_HEIGHT -100
#define ROOF_TIMEOUT 2
#define SUPPORT_TIMEOUT 8
#define AI_TIMEOUT 8
#define AI_RELEASE_SPEED 15
#define AI_DISTANCE_SQUARED 22500

static float
store_float(long double value)
{
	volatile float stored;

	stored = (float)value;

	return stored;
}

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

/* With exactly two contacts, only a pair on the same side holds the timer. */
static int
support_timer_advances(const int supported[4], unsigned int count)
{
	return count < 2 ||
		(count == 2 && supported[0] + supported[3] != 2 &&
			supported[1] + supported[2] != 2);
}

static void
update_ai_timeout(struct mh_recovery_result *result,
	const struct mh_recovery_feedback *feedback, float step_seconds)
{
	unsigned int axis;
	float delta[3];
	long double ai_elapsed, camera_distance_squared;

	if (fabs(feedback->forward_speed) > AI_RELEASE_SPEED)
		result->state.ai_latched = 0;

	if (feedback->reversing && !result->state.ai_latched) {
		result->state.ai_latched = 1;
		result->state.ai_seconds = 0;
	}

	if (result->state.ai_latched) {
		ai_elapsed =
			(long double)result->state.ai_seconds + step_seconds;
		result->state.ai_seconds = store_float(ai_elapsed);

		/*
		 * Store the float but compare the unrounded sum.
		 */
		if (ai_elapsed > AI_TIMEOUT) {
			for (axis = 0; axis < 3; axis++)
				delta[axis] = store_float(
					(long double)feedback->position[axis] -
					feedback->camera[axis]);

			camera_distance_squared =
				(long double)delta[1] * delta[1] +
				(long double)delta[0] * delta[0];
			camera_distance_squared +=
				(long double)delta[2] * delta[2];

			if (camera_distance_squared > AI_DISTANCE_SQUARED) {
				result->state.ai_latched = 0;
				result->reason = MH_RECOVERY_AI;
			}
		}
	}
}

static enum mh_error_code
evaluate_recovery(const struct mh_recovery_state *state,
	const struct mh_recovery_feedback *feedback, double dt, int check_fall,
	struct mh_recovery_result *output, struct mh_error *error_out)
{
	struct mh_recovery_result result;
	unsigned int axis, wheel, supported_count;
	float step_seconds;

	if (state == NULL || feedback == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recovery arguments");

	if (!(dt > 0 && dt <= TIMER_LIMIT) ||
		!(state->roof_seconds >= 0 &&
			state->roof_seconds <= TIMER_LIMIT) ||
		!(state->unsupported_seconds >= 0 &&
			state->unsupported_seconds <= TIMER_LIMIT) ||
		!(state->ai_seconds >= 0 && state->ai_seconds <= TIMER_LIMIT) ||
		!valid_flag(state->ai_latched) ||
		!(feedback->forward_speed >= -SPEED_LIMIT &&
			feedback->forward_speed <= SPEED_LIMIT) ||
		!valid_flag(feedback->ai_controlled) ||
		!valid_flag(feedback->reversing) ||
		!valid_flag(feedback->roof_contact) ||
		!valid_flag(feedback->racing) ||
		!valid_flag(feedback->finished)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recovery state or feedback");
	}

	for (axis = 0; axis < 3; axis++)
		if (!(feedback->position[axis] >= -POSITION_LIMIT &&
			feedback->position[axis] <= POSITION_LIMIT) ||
			!(feedback->camera[axis] >= -POSITION_LIMIT &&
				feedback->camera[axis] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid recovery position");

	supported_count = 0;

	for (wheel = 0; wheel < 4; wheel++) {
		if (!valid_flag(feedback->supported[wheel]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid recovery wheel support");

		supported_count += feedback->supported[wheel];
	}

	result.state = *state;
	result.reason = MH_RECOVERY_NONE;
	step_seconds = store_float(dt);

	if (step_seconds > STEP_LIMIT)
		step_seconds = STEP_LIMIT;

	if (check_fall && feedback->position[1] < FALL_HEIGHT)
		result.reason = MH_RECOVERY_FALL;
	else {
		if (feedback->roof_contact)
			result.state.roof_seconds =
				store_float((long double)state->roof_seconds +
					step_seconds);

		if (result.state.roof_seconds > ROOF_TIMEOUT)
			result.reason = MH_RECOVERY_ROOF;
		else {
			if (support_timer_advances(
				feedback->supported, supported_count))
				result.state.unsupported_seconds = store_float(
					(long double)
						state->unsupported_seconds +
					step_seconds);

			if (supported_count > 2)
				result.state.roof_seconds =
					result.state.unsupported_seconds = 0;

			if (result.state.unsupported_seconds > SUPPORT_TIMEOUT)
				result.reason = MH_RECOVERY_SUPPORT;
		}

		if (result.reason == MH_RECOVERY_NONE &&
			feedback->ai_controlled)
			update_ai_timeout(&result, feedback, step_seconds);

		if (!feedback->racing || feedback->finished)
			result.reason = MH_RECOVERY_NONE;
	}

	if (result.state.roof_seconds > TIMER_LIMIT ||
		result.state.unsupported_seconds > TIMER_LIMIT ||
		result.state.ai_seconds > TIMER_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recovery timer limit reached");

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_recovery_step(const struct mh_recovery_state *state,
	const struct mh_recovery_feedback *feedback, double dt,
	struct mh_recovery_result *output, struct mh_error *error_out)
{
	return evaluate_recovery(state, feedback, dt, 1, output, error_out);
}

enum mh_error_code
mh_recovery_timeout(const struct mh_recovery_state *state,
	const struct mh_recovery_feedback *feedback, double dt,
	struct mh_recovery_result *output, struct mh_error *error_out)
{
	return evaluate_recovery(state, feedback, dt, 0, output, error_out);
}
