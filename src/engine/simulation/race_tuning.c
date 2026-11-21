#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/race_tuning.h"

#define PROGRESS_LIMIT 1e9f
#define BIAS_LIMIT 1e6f
#define BASE_ENGINE_LIMIT 1e6f
#define TUNED_ENGINE_LIMIT 1e7f
#define POINT_COUNT_LIMIT 100000
#define ACTIVE_MASK_LIMIT (1U << MH_RACE_TUNING_CARS)
#define CURVE_SPREAD_LIMIT 50
#define CURVE_SCALE .02
#define ADJUSTMENT_LIMIT 2
#define PROGRESS_WRAP_FRACTION .7
#define LAUNCH_DURATION_MS 7000
#define LAUNCH_WEIGHT .25
#define STRENGTH_LIMIT 100
#define STRENGTH_SCALE .01f

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

static int
valid_state(const struct mh_race_tuning *state)
{
	unsigned int i;

	if (state == NULL || !bounded(state->local_bias, BIAS_LIMIT))
		return 0;

	for (i = 0; i < MH_RACE_TUNING_CARS; i++)
		if (!bounded(state->progress[i], PROGRESS_LIMIT) ||
			!bounded(state->bias[i], BIAS_LIMIT))
			return 0;

	return 1;
}

static int
valid_engine(const struct mh_race_tuning_engine *engine)
{
	unsigned int i;

	if (engine == NULL || engine->gears > MH_RACE_TUNING_GEARS ||
		!bounded(engine->base_acceleration, BASE_ENGINE_LIMIT) ||
		!bounded(engine->acceleration, TUNED_ENGINE_LIMIT))
		return 0;

	for (i = 0; i <= MH_RACE_TUNING_GEARS; i++)
		if (!bounded(engine->base_ratio[i], BASE_ENGINE_LIMIT) ||
			!bounded(engine->ratio[i], TUNED_ENGINE_LIMIT))
			return 0;

	return 1;
}

enum mh_error_code
mh_race_tuning_curve(struct mh_race_tuning *state, unsigned int count,
	unsigned int local_rank, struct mh_error *error_out)
{
	struct mh_race_tuning next;
	float spread, delta, endpoint;
	long double value;
	unsigned int i;

	if (!valid_state(state) || count < 1 || count > MH_RACE_TUNING_CARS ||
		local_rank > MH_RACE_TUNING_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race tuning curve");

	next = *state;
	spread = (float)((long double)next.progress[0] -
		next.progress[count - 1]);

	if (count == 1 || spread <= 0) {
		for (i = 0; i < count; i++)
			next.bias[i] = 0;
	} else {
		endpoint = spread > CURVE_SPREAD_LIMIT
			? 1
			: (float)((long double)spread * (double)CURVE_SCALE);
		next.bias[0] = -endpoint;
		next.bias[count - 1] = endpoint;
		delta = (float)((long double)next.bias[0] -
			next.bias[count - 1]);

		for (i = 1; i + 1 < count; i++) {
			value = ((long double)next.progress[i] -
					next.progress[count - 1]) /
				spread;
			value = value * delta + next.bias[count - 1];
			next.bias[i] = (float)value;
		}
	}

	next.local_bias = local_rank == 0 ? 0 : next.bias[local_rank - 1];

	if (!valid_state(&next))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"race tuning curve exceeds native bounds");

	*state = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
apply_engine_adjustment(
	float adjustment, float weight, struct mh_race_tuning_engine *engine)
{
	float factor;
	unsigned int i;

	if (adjustment > ADJUSTMENT_LIMIT)
		adjustment = ADJUSTMENT_LIMIT;

	if (adjustment < -ADJUSTMENT_LIMIT)
		adjustment = -ADJUSTMENT_LIMIT;

	factor = (float)((long double)adjustment * weight);
	factor = (float)((long double)factor + 1);

	for (i = 1; i <= engine->gears; i++)
		engine->ratio[i] =
			(float)((long double)factor * engine->base_ratio[i]);

	engine->acceleration =
		(float)((long double)engine->base_acceleration * factor);
}

enum mh_error_code
mh_race_tuning_car(struct mh_race_tuning *state, unsigned long rank,
	unsigned int lap, int finished, unsigned long point_count,
	unsigned long point, unsigned long checkpoint_two, float weight,
	struct mh_race_tuning_engine *engine, struct mh_error *error_out)
{
	struct mh_race_tuning next;
	struct mh_race_tuning_engine tuned;
	unsigned int rank_index;
	unsigned long lap_base;
	float adjustment;

	if (!valid_state(state) || !valid_engine(engine) || rank > MH_U32_MAX ||
		lap < 1 || lap > MH_RACE_MAX_LAPS ||
		(finished != 0 && finished != 1) || point_count < 1 ||
		point_count > POINT_COUNT_LIMIT || point >= point_count ||
		checkpoint_two > MH_U32_MAX || !bounded(weight, 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race tuning car");

	next = *state;
	tuned = *engine;

	if (rank < 1)
		rank_index = 0;
	else if (rank > MH_RACE_TUNING_CARS)
		rank_index = MH_RACE_TUNING_CARS - 1;
	else
		rank_index = (unsigned int)rank - 1;

	lap_base = (lap - 1 + (unsigned int)finished) * point_count;
	next.progress[rank_index] = (float)(lap_base + point);

	if (checkpoint_two == 0 &&
		(long double)lap_base +
				(long double)point_count *
					(double)PROGRESS_WRAP_FRACTION <
			next.progress[rank_index])
		next.progress[rank_index] =
			(float)((long double)next.progress[rank_index] -
				point_count);

	adjustment =
		(float)((long double)next.bias[rank_index] - next.local_bias);
	apply_engine_adjustment(adjustment, weight, &tuned);
	*state = next;
	*engine = tuned;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_tuning_launch(unsigned int count, unsigned long rank, float weight,
	struct mh_race_tuning_engine *engine, struct mh_error *error_out)
{
	struct mh_race_tuning_engine tuned;
	long double value;
	float step, adjustment;

	if (count < 1 || count > MH_RACE_TUNING_CARS || rank > MH_U32_MAX ||
		!bounded(weight, 1) || !valid_engine(engine))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race launch tuning");

	if (count == 1) {
		/*
		 * 0*infinity is unordered, taking the original lower-clamp
		 * branch.
		 */
		adjustment = rank == 1 ? -ADJUSTMENT_LIMIT : ADJUSTMENT_LIMIT;
	} else {
		step = (float)(2 / ((long double)count - 1));
		value = (long double)((rank - 1) & MH_U32_MAX) * step - 1;
		adjustment = value > ADJUSTMENT_LIMIT ? ADJUSTMENT_LIMIT
						: (float)value;
	}

	tuned = *engine;
	apply_engine_adjustment(adjustment, weight, &tuned);
	*engine = tuned;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_tuning_schedule(unsigned int mode, int enabled, unsigned long go_ms,
	long double now_ms, float strength, unsigned int active_mask,
	unsigned int local_slot, struct mh_race_tuning_plan *output,
	struct mh_error *error_out)
{
	struct mh_race_tuning_plan plan;
	long double value;

	if ((enabled != 0 && enabled != 1) || go_ms > MH_U32_MAX ||
		!(now_ms >= 0 && now_ms <= (double)MH_U32_MAX) ||
		!(strength >= 0 && strength <= STRENGTH_LIMIT) ||
		active_mask >= ACTIVE_MASK_LIMIT ||
		local_slot >= MH_RACE_TUNING_CARS || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race tuning schedule");

	plan.phase = MH_TUNING_NONE;
	plan.suppress_role_two_spin = 0;
	plan.apply_mask = 0;
	plan.weight = 0;

	if (mode == MH_GAME_QUICK_RACE || mode == MH_GAME_LEAGUE ||
		mode == MH_GAME_ATTRACT) {
		plan.suppress_role_two_spin = 1;

		if (now_ms < ((go_ms + LAUNCH_DURATION_MS) & MH_U32_MAX)) {
			plan.phase = MH_TUNING_LAUNCH;
			plan.apply_mask = active_mask & ~(1U << local_slot);
			value = ((long double)now_ms - go_ms) *
				(double)(1.0 / LAUNCH_DURATION_MS);
			plan.weight = (float)(value * (double)LAUNCH_WEIGHT +
				(double)-LAUNCH_WEIGHT);
		} else if (enabled && mode != MH_GAME_ATTRACT) {
			plan.phase = MH_TUNING_CATCHUP;
			plan.apply_mask = active_mask;
			plan.weight = (float)((long double)strength *
				(float)STRENGTH_SCALE);
		}
	}

	*output = plan;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
