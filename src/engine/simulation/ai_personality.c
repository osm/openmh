#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/simulation/ai_personality.h"

#define INITIAL_SCALE 0.5f
#define SMALL_STEP 0.01f
#define EAGERNESS_STEP 0.05f
#define UPWARD_DRAW_MIN 16383
/* Native bounds allow the original one-step overshoot. */
#define WALK_MIN -0.051f
#define WALK_MAX 1.051f

enum mh_error_code
mh_ai_personality_start(const float parameters[MH_AI_PERSONALITY_PARAMETERS],
	struct mh_ai_personality *output, struct mh_error *error_out)
{
	struct mh_ai_personality next;
	unsigned int i;

	if (parameters == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing AI personality");

	for (i = 0; i < MH_AI_PERSONALITY_PARAMETERS; i++) {
		if (!(parameters[i] >= 0 && parameters[i] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid AI personality parameter");

		next.walk[i] = parameters[i] * INITIAL_SCALE;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_personality_step(struct mh_ai_personality *state, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_ai_personality next;
	unsigned int i, draw;
	unsigned long seed;
	float step;

	if (state == NULL || random == NULL || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI personality state");

	for (i = 0; i < MH_AI_PERSONALITY_PARAMETERS; i++)
		if (!(state->walk[i] >= WALK_MIN && state->walk[i] <= WALK_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid AI personality walk");

	next = *state;
	seed = *random;

	for (i = 0; i < MH_AI_PERSONALITY_PARAMETERS; i++) {
		draw = mh_random_next(&seed);
		step = i == MH_AI_EAGERNESS ? EAGERNESS_STEP : SMALL_STEP;

		if (draw >= UPWARD_DRAW_MIN) {
			if (next.walk[i] < 1)
				next.walk[i] = (float)(next.walk[i] + step);
		} else if (next.walk[i] > 0)
			next.walk[i] = (float)(next.walk[i] - step);
	}

	*state = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_personality_decisions(const struct mh_ai_personality *state,
	float aggressiveness, float eagerness, int *avoid_sides, int *boosted,
	struct mh_error *error_out)
{
	if (state == NULL || avoid_sides == NULL || boosted == NULL ||
		!(aggressiveness >= 0 && aggressiveness <= 1) ||
		!(eagerness >= 0 && eagerness <= 1) ||
		!(state->walk[MH_AI_AGGRESSIVENESS] >= WALK_MIN &&
			state->walk[MH_AI_AGGRESSIVENESS] <= WALK_MAX) ||
		!(state->walk[MH_AI_EAGERNESS] >= WALK_MIN &&
			state->walk[MH_AI_EAGERNESS] <= WALK_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI personality decisions");

	/* Compare before rounding the difference to float. */
	*avoid_sides = 1.0 - (double)aggressiveness >
		(double)state->walk[MH_AI_AGGRESSIVENESS];
	*boosted =
		1.0 - (double)eagerness < (double)state->walk[MH_AI_EAGERNESS];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_personality_league_start(float aggressiveness, float eagerness,
	struct mh_ai_personality *output, struct mh_error *error_out)
{
	float parameters[MH_AI_PERSONALITY_PARAMETERS];

	parameters[MH_AI_AGGRESSIVENESS] = aggressiveness;
	parameters[MH_AI_EAGERNESS] = eagerness;
	parameters[MH_AI_THIRD_PARAMETER] = 0;

	return mh_ai_personality_start(parameters, output, error_out);
}
