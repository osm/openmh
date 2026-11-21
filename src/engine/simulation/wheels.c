#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/simulation/wheels.h"

/* Native bounds, distinct from animation thresholds below. */
#define RATIO_LIMIT 20000
#define RPM_LIMIT 1000000
#define VELOCITY_LIMIT 10000
#define GEAR_LIMIT 9
#define ANIMATION_LIMIT 1000000
#define ELAPSED_LIMIT 1

#define SPIN_RATE_SCALE .00006
#define ANGLE_WRAP_THRESHOLD 125.6637
#define ANGLE_WRAP_AMOUNT 125.6636962890625
#define BLUR_TRAVEL .8
#define BLUR_VARIANT_STEP (1.0 / 3.0)
#define FASTEST_VARIANT 3
#define FRAME_ADVANCE_MIN .0001

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

enum mh_error_code
mh_wheel_rates(const struct mh_wheel_input *input, double rates[2],
	struct mh_error *error_out)
{
	double road_rate, spin_rate, rear_rate;

	if (input == NULL || rates == NULL ||
		!(input->ratio >= 1 && input->ratio <= RATIO_LIMIT) ||
		!(input->rpm >= 0 && input->rpm <= RPM_LIMIT) ||
		!(input->forward_velocity >= -VELOCITY_LIMIT &&
			input->forward_velocity <= VELOCITY_LIMIT) ||
		input->gear > GEAR_LIMIT || !valid_flag(input->heavy_brake) ||
		!valid_flag(input->wheelspin) ||
		!valid_flag(input->rear_supported) ||
		!valid_flag(input->up_ready) || !valid_flag(input->down_ready))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid wheel rate input");

	road_rate = spin_rate = 0;

	if (!input->heavy_brake) {
		spin_rate = (double)(float)((long double)(float)input->ratio *
			(long double)SPIN_RATE_SCALE * (float)input->rpm);
		road_rate = (double)(float)((long double)(float)
						input->forward_velocity *
			2);
	}

	if (input->wheelspin)
		rear_rate = spin_rate;
	else if (input->rear_supported || !input->up_ready ||
		!input->down_ready)
		rear_rate = road_rate;
	else
		rear_rate = input->gear == 0 ? -spin_rate : spin_rate;

	rates[0] = road_rate;
	rates[1] = rear_rate;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
blur_variant(long double travel)
{
	if (travel > (float)BLUR_TRAVEL)
		return FASTEST_VARIANT;

	return (unsigned int)((long double)(float)travel /
		((long double)(float)BLUR_TRAVEL *
			(long double)BLUR_VARIANT_STEP));
}

enum mh_error_code
mh_wheel_advance(const struct mh_wheel_state *state, const double rates[2],
	double elapsed_seconds, struct mh_wheel_state *output,
	struct mh_error *error_out)
{
	struct mh_wheel_state result;
	long double travel;
	double angle;
	unsigned int axle;

	if (state == NULL || rates == NULL || output == NULL ||
		!(elapsed_seconds >= 0 && elapsed_seconds <= ELAPSED_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid wheel animation");

	result = *state;

	for (axle = 0; axle < 2; axle++) {
		if (!(state->angle[axle] >= -ANIMATION_LIMIT &&
			state->angle[axle] <= ANIMATION_LIMIT) ||
			!(rates[axle] >= -ANIMATION_LIMIT &&
				rates[axle] <= ANIMATION_LIMIT) ||
			state->variant[axle] > FASTEST_VARIANT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid wheel animation channel");

		angle = (double)(float)((long double)(float)rates[axle] *
				(float)elapsed_seconds +
			(float)state->angle[axle]);

		/*
		 * Compare in binary64, then subtract the widened binary32
		 * value.
		 */
		if (angle > ANGLE_WRAP_THRESHOLD)
			angle = (double)(float)(angle - ANGLE_WRAP_AMOUNT);

		if (angle < -ANGLE_WRAP_THRESHOLD)
			angle = (double)(float)(angle + ANGLE_WRAP_AMOUNT);

		result.angle[axle] = angle;
		travel = (long double)fabs((double)(float)rates[axle]) *
			(float)elapsed_seconds;
		result.variant[axle] = blur_variant(travel);
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_wheel_frame(const struct mh_wheel_state *state, const double rates[2],
	float elapsed_seconds, struct mh_wheel_state *output,
	struct mh_error *error_out)
{
	struct mh_wheel_state result;
	enum mh_error_code status;
	unsigned int axle;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing wheel frame output");

	status = mh_wheel_advance(
		state, rates, elapsed_seconds, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Retain all animation state when the original caller skips the update.
	 * The preceding model binding still observes the previous variants.
	 */
	if (elapsed_seconds <= FRAME_ADVANCE_MIN)
		result = *state;

	for (axle = 0; axle < 2; axle++)
		result.drawn[axle] = state->variant[axle];

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
