#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/ai_personality.h"
static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_ai_personality state, before;
	float parameters[3];
	unsigned long seed;
	int avoid, boosted;

	CHECK(mh_ai_personality_league_start(.25f, .75f, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.walk[0] == .125f && state.walk[1] == .375f &&
		state.walk[2] == 0);
	parameters[0] = 0;
	parameters[1] = .5f;
	parameters[2] = 1;
	CHECK(mh_ai_personality_start(parameters, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.walk[0] == 0 && state.walk[1] == .25f &&
		state.walk[2] == .5f);
	state.walk[0] = state.walk[1] = .5f;
	CHECK(mh_ai_personality_decisions(&state, .5f, .5f, &avoid, &boosted,
		NULL) == MH_ERROR_NONE);
	CHECK(!avoid && !boosted);
	state.walk[0] = .49f;
	state.walk[1] = .51f;
	CHECK(mh_ai_personality_decisions(&state, .5f, .5f, &avoid, &boosted,
		NULL) == MH_ERROR_NONE);
	CHECK(avoid && boosted);
	CHECK(mh_ai_personality_decisions(
		&state, 2, .5f, &avoid, &boosted, NULL) != MH_ERROR_NONE);
	CHECK(avoid && boosted);
	before = state;
	parameters[2] = 2;
	CHECK(mh_ai_personality_start(parameters, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &before, sizeof(state)) == 0);
	state.walk[1] = 2;
	before = state;
	seed = 1;
	CHECK(mh_ai_personality_step(&state, &seed, NULL) != MH_ERROR_NONE &&
		seed == 1 && memcmp(&state, &before, sizeof(state)) == 0);

	return failures != 0;
}
