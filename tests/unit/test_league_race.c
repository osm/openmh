#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/league_race.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned long divisions[4] = {400, 300, 200, 100};
	static const unsigned int starts[5] = {0, 2, 10, 18, 26};
	struct mh_league league, saved_league, expected;
	struct mh_league_race race, saved_race;
	struct mh_standings standings;
	struct mh_league_roster roster, saved_roster;
	unsigned int drivers[8], group, count, i, j, repeat, index;
	unsigned long seed, reference_seed;

	for (i = 0; i < 26; i++) {
		memset(&league, 0, sizeof(league));

		for (group = 0; group < 26; group++)
			league.entries[group].driver = 100 + (group * 7) % 26;

		league.entries[i].human = 1;
		CHECK(mh_league_roster(&league, &roster, NULL) ==
			MH_ERROR_NONE);
		CHECK(roster.drivers[0] == 100 + (i * 7) % 26);
		CHECK(roster.count == (i < 2 ? 2U : 8U));

		for (j = 0; j < roster.count; j++) {
			for (index = 0; index < 26 &&
				league.entries[index].driver !=
					roster.drivers[j];
				index++) {
			}

			CHECK(index < 26);
			CHECK(roster.slots[j] ==
				(roster.stage == 0
						? (j == 0 ? 1U : 0U)
						: 7 - (index - starts[roster.stage])));
		}

		saved_roster = roster;
		league.entries[i].human = 0;
		CHECK(mh_league_roster(&league, &roster, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&saved_roster, &roster, sizeof(roster)) == 0);
		league.entries[i].human = league.entries[(i + 1) % 26].human =
			1;
		CHECK(mh_league_roster(&league, &roster, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&saved_roster, &roster, sizeof(roster)) == 0);
	}

	for (group = 0; group < 4; group++) {
		memset(&league, 0, sizeof(league));

		for (i = 0; i < 26; i++)
			league.entries[i].driver = 100 + i;

		league.entries[starts[group] + 1].human = 1;
		saved_league = league;
		expected = league;
		count = starts[group + 1] - starts[group];
		memset(&standings, 0, sizeof(standings));
		standings.count = count;

		for (i = 0; i < count; i++) {
			drivers[i] =
				league.entries[starts[group] + (i + 1) % count]
					.driver;
			standings.cars[i].position = i + 1;
			standings.cars[i].estimated = 1;
		}

		seed = reference_seed = 1;
		CHECK(mh_league_prepare_positions(&expected, &reference_seed,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_league_race_begin(&league, divisions, 4, drivers,
			count, &seed, &race, NULL) == MH_ERROR_NONE);
		CHECK(seed == reference_seed &&
			memcmp(&league, &saved_league, sizeof(league)) == 0);
		CHECK(mh_league_apply_results(&expected, &standings, drivers,
			(1U << count) - 1, 1, NULL) == MH_ERROR_NONE);
		saved_race = race;
		standings.cars[0].position = count + 1;
		CHECK(mh_league_race_finish(&race, &standings, 1, &league,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);
		CHECK(memcmp(&league, &saved_league, sizeof(league)) == 0);
		standings.cars[0].position = 1;
		league.entries[25].wins++;
		CHECK(mh_league_race_finish(&race, &standings, 1, &league,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);
		league = saved_league;
		CHECK(mh_league_race_finish(&race, &standings, 1, &league,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&league, &expected, sizeof(league)) == 0);
		CHECK(league.entries[starts[group]].driver == drivers[0]);
		CHECK(league.entries[starts[group]].points == 12 &&
			league.race_index == 1);
		CHECK(!standings.cars[0].finished &&
			standings.cars[0].estimated);

		for (repeat = 0; repeat < 100; repeat++)
			CHECK(mh_league_race_finish(&race, &standings, 1,
				&league, NULL) == MH_ERROR_NONE);

		CHECK(memcmp(&league, &expected, sizeof(league)) == 0);
		league = saved_league;
		CHECK(mh_league_race_finish(&race, &standings, 1, &league,
			NULL) != MH_ERROR_NONE);
		saved_race = race;
		reference_seed = seed;
		drivers[0] = drivers[1];
		CHECK(mh_league_race_begin(&league, divisions, 4, drivers,
			count, &seed, &race, NULL) != MH_ERROR_NONE);
		CHECK(seed == reference_seed &&
			memcmp(&race, &saved_race, sizeof(race)) == 0);
		drivers[0] = league.entries[starts[group] + 1].driver;
		league.race_index = 4;
		CHECK(mh_league_race_begin(&league, divisions, 4, drivers,
			count, &seed, &race, NULL) != MH_ERROR_NONE);
		CHECK(seed == reference_seed &&
			memcmp(&race, &saved_race, sizeof(race)) == 0);
		league = saved_league;
		league.entries[group == 0 ? 18 : 0].human = 1;
		CHECK(mh_league_race_begin(&league, divisions, 4, drivers,
			count, &seed, &race, NULL) != MH_ERROR_NONE);
		CHECK(seed == reference_seed &&
			memcmp(&race, &saved_race, sizeof(race)) == 0);
	}

	return 0;
}
