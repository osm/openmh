#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/intro_camera.h"

#define CLOCK_LIMIT 8589934592.0
#define FIRST_START_MS 6000UL
#define SECOND_START_MS 8000UL
#define RESTORE_START_MS 10000UL
#define RESTORE_END_MS 12000UL
#define MOTION_FRAMES_PER_MS .0125

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

static void
append_sample(struct mh_intro_camera_result *result, unsigned int motion_index,
	float frame)
{
	result->samples[result->count].motion_index = motion_index;

	result->samples[result->count++].frame = frame;
	result->state.in_car = result->state.requested_in_car =
		result->state.track = 0;
	result->state.chase = 1;
}

enum mh_error_code
mh_intro_camera_step(const struct mh_intro_camera_state *state,
	const struct mh_intro_camera_input *input,
	struct mh_intro_camera_result *output, struct mh_error *error_out)
{
	struct mh_intro_camera_result result;
	double first_start_ms, second_start_ms, restore_start_ms,
		restore_end_ms;

	if (state == NULL || input == NULL || output == NULL ||
		!valid_flag(state->enabled) ||
		!valid_flag(state->saved_in_car) ||
		!valid_flag(state->in_car) ||
		!valid_flag(state->requested_in_car) ||
		!valid_flag(state->chase) || !valid_flag(state->track) ||
		!valid_flag(input->has_first_motion) ||
		!valid_flag(input->has_second_motion) ||
		input->start_ms > MH_U32_MAX ||
		!(input->now_ms >= 0 && input->now_ms <= CLOCK_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid opening camera state or clock");

	memset(&result, 0, sizeof(result));
	result.state = *state;

	if (state->enabled) {
		first_start_ms = (double)((input->start_ms + FIRST_START_MS) &
			MH_U32_MAX);
		second_start_ms = (double)((input->start_ms + SECOND_START_MS) &
			MH_U32_MAX);
		restore_start_ms =
			(double)((input->start_ms + RESTORE_START_MS) &
				MH_U32_MAX);
		restore_end_ms = (double)((input->start_ms + RESTORE_END_MS) &
			MH_U32_MAX);

		if (input->has_first_motion &&
			(input->start_ms == 0 ||
				input->now_ms < first_start_ms))
			append_sample(&result, 0, 0);

		if (input->has_first_motion && input->now_ms > first_start_ms &&
			input->now_ms < second_start_ms)
			append_sample(&result, 0,
				(float)(((long double)input->now_ms -
						first_start_ms) *
					(long double)MOTION_FRAMES_PER_MS));

		if (input->has_second_motion &&
			input->now_ms > second_start_ms &&
			input->now_ms < restore_start_ms)
			append_sample(&result, 1,
				(float)(((long double)input->now_ms -
						second_start_ms) *
					(long double)MOTION_FRAMES_PER_MS));

		if (input->now_ms > restore_start_ms &&
			input->now_ms < restore_end_ms) {
			result.state.enabled = 0;
			result.state.in_car = result.state.requested_in_car =
				state->saved_in_car;
			result.state.chase = !state->saved_in_car;
			result.state.track = 0;
		}
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
