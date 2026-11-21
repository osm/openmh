#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league_unlocks.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League unlocks line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned long divisions[8] = {
		100, 101, 200, 201, 300, 301, 310, 400};

	static const unsigned int positions[7] = {18, 19, 10, 11, 2, 0, 25};
	static const unsigned int sizes[7] = {2, 2, 4, 4, 7, 1, 0};
	struct mh_league league;
	struct mh_league_unlock_plan plan, saved_plan;
	unsigned long repeated_divisions[64];
	unsigned int i, j;

	for (i = 0; i < 7; i++) {
		memset(&league, 0, sizeof(league));
		league.race_index = 9;
		league.entries[positions[i]].human = MH_U32_SIGN_BIT;
		CHECK(mh_league_unlock_plan(&league, divisions, 8, &plan,
			NULL) == MH_ERROR_NONE &&
			plan.count == sizes[i]);

		for (j = 0; j < plan.count; j++)
			CHECK(plan.tracks[j] == (i == 5 ? 7U : j));

		league.race_index = 0;
		CHECK(mh_league_unlock_plan(&league, divisions, 8, &plan,
			NULL) == MH_ERROR_NONE &&
			plan.count == 0);
	}

	saved_plan = plan;
	league.race_index = 999;

	for (i = 0; i < 64; i++)
		repeated_divisions[i] = 100;

	CHECK(mh_league_unlock_plan(&league, repeated_divisions, 64, &plan,
		NULL) != MH_ERROR_NONE &&
		memcmp(&plan, &saved_plan, sizeof(plan)) == 0);
	CHECK(mh_league_unlock_plan(&league, divisions, 65, &plan, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&plan, &saved_plan, sizeof(plan)) == 0);
	CHECK(mh_league_unlock_plan(NULL, divisions, 8, &plan, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&plan, &saved_plan, sizeof(plan)) == 0);
	puts("League unlock winners, ordinary/special lists, completion gate and capacity checks passed");

	return 0;
}
