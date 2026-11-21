#ifndef MH_AI_PERSONALITY_H
#define MH_AI_PERSONALITY_H

#include "engine/core/error.h"

enum mh_ai_personality_index {
	MH_AI_AGGRESSIVENESS,
	MH_AI_EAGERNESS,
	/* Preserved third parameter; its gameplay meaning is unresolved. */
	MH_AI_THIRD_PARAMETER,
	MH_AI_PERSONALITY_PARAMETERS
};

/* Values are stored as binary32. */
struct mh_ai_personality {
	float	walk[MH_AI_PERSONALITY_PARAMETERS];
};

/*
 * Fresh session initializes the third parameter to zero; league roster
 * construction preserves it thereafter.
 */
enum mh_error_code	mh_ai_personality_start(
	const float parameters[MH_AI_PERSONALITY_PARAMETERS],
	struct mh_ai_personality *output, struct mh_error *error_out);

/*
 * Fresh league session: two ADP parameters, zero third parameter from the
 * original session clear. Existing-session reset uses start with saved values.
 */
enum mh_error_code	mh_ai_personality_league_start(float aggressiveness,
	float eagerness, struct mh_ai_personality *output,
	struct mh_error *error_out);

/*
 * Three shared random draws, even at endpoints. Output and RNG unchanged on
 * invalid input.
 */
enum mh_error_code	mh_ai_personality_step(struct mh_ai_personality *state,
	unsigned long *random, struct mh_error *error_out);

/*
 * Gates use current walks: side avoidance is strict >, boosting strict <.
 * Caller places these relative to updates according to the driving pass.
 */
enum mh_error_code	mh_ai_personality_decisions(
	const struct mh_ai_personality *state, float aggressiveness,
	float eagerness, int *avoid_sides, int *boosted,
	struct mh_error *error_out);
#endif
