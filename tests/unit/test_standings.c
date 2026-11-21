#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league.h"
#include "engine/game/league_document.h"
#include "engine/game/result_prepare.h"
#include "engine/game/standings.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
league_save(void)
{
	struct mh_league league;
	struct mh_profile profiles[26];
	struct mh_league_document loaded, saved;
	unsigned char *wrapped, *decoded;
	size_t wrapped_size, decoded_size;
	unsigned char *text, *sentinel;
	size_t size;
	unsigned int i, j, identity;

	memset(&league, 0, sizeof(league));
	memset(profiles, 0, sizeof(profiles));

	for (i = 0; i < 26; i++) {
		league.entries[i].driver = 25 - i;
		sprintf(profiles[i].name, "Driver%u", i);
		sprintf(profiles[i].nick, "Nick%u", i);
		strcpy(profiles[i].horn, "Horn3");
		profiles[i].league_fields = 31;
		profiles[i].eagerness = (double)i / 32;
		profiles[i].colors[1] = (unsigned long)i * 0x010101UL;

		for (j = 0; j < 4; j++)
			strcpy(profiles[i].cars[j], "ASC II");
	}

	league.entries[0].human = 1;
	league.entries[0].points = MH_U32_MAX;
	CHECK(mh_league_save_text(&league, profiles, 26, "Test",
		MH_U32_SIGN_BIT, MH_U32_MAX, MH_U32_SIGN_BIT, &text,
		&size, NULL) == MH_ERROR_NONE);
	CHECK(strstr((const char *)text, "CreateDate\t-2147483648\n") != NULL);
	CHECK(strstr((const char *)text,
		"RacesDone\t-1\nLapsDone\t-2147483648\n") != NULL);

	/* Save nicknames for humans as well as AI. */
	CHECK(strstr((const char *)text, "HumanPlayer\tNick25\n") != NULL);
	CHECK(strstr((const char *)text, "Score\t-1\n") != NULL);
	CHECK(strstr((const char *)text, "AIPlayer\tNick0\n") != NULL);
	CHECK(text[size] == 0 && size < 16384);
	CHECK(mh_tbl_encode_named(text, size, "Test.lgf", &wrapped,
		&wrapped_size, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_decode_named(wrapped, wrapped_size, "Test.lgf", &decoded,
		&decoded_size, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_load_text(decoded, decoded_size, profiles, 26, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.name, "Test") == 0 &&
		loaded.created == MH_U32_SIGN_BIT);
	CHECK(loaded.races == MH_U32_MAX && loaded.laps == MH_U32_SIGN_BIT);
	CHECK(loaded.league.entries[0].points == MH_U32_MAX &&
		loaded.league.entries[0].human == 1);

	for (i = 0; i < 26; i++) {
		CHECK(strcmp(loaded.profiles[loaded.league.entries[i].driver]
				.name,
			i == 0 ? profiles[25 - i].nick
				: profiles[25 - i].name) == 0);
		CHECK(strcmp(loaded.profiles[i].cars[2], "ASC II") == 0);
	}

	saved = loaded;
	CHECK(mh_league_load_text(decoded, decoded_size, NULL, 0, &loaded,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved, sizeof(loaded)) == 0);
	CHECK(mh_league_load_text(decoded, decoded_size - 10, profiles, 26,
		&loaded, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved, sizeof(loaded)) == 0);
	free(wrapped);
	free(decoded);
	free(text);

	/*
	 * A human wins the bottom division; its profile must follow promotion.
	 */
	loaded.league.entries[0].human = 0;

	/* Restore AI nickname/metadata after changing its role. */
	loaded.profiles[0] = profiles[25];
	loaded.league.entries[18].human = 1;
	loaded.league.race_index = 7;
	CHECK(mh_league_rollover(&loaded.league, NULL) == MH_ERROR_NONE);
	CHECK(loaded.league.entries[16].human == 1 &&
		loaded.league.entries[16].driver == 18);
	saved = loaded;
	CHECK(mh_league_save_text(&loaded.league, loaded.profiles, 26,
		loaded.name, loaded.created, loaded.races, loaded.laps,
		&text, &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_load_text(text, size, profiles, 26, &loaded, NULL) ==
		MH_ERROR_NONE);
	CHECK(loaded.league.race_index == 0 &&
		loaded.league.entries[16].human == 1);
	CHECK(loaded.created == saved.created && loaded.races == saved.races &&
		loaded.laps == saved.laps);

	for (i = 0; i < 26; i++) {
		identity = saved.league.entries[i].driver;
		CHECK(strcmp(loaded.profiles[i].name,
			loaded.league.entries[i].human
				? saved.profiles[identity].nick
				: saved.profiles[identity].name) == 0);
		CHECK(loaded.league.entries[i].points == 0 &&
			loaded.league.entries[i].wins == 0);

		if (!loaded.league.entries[i].human) {
			CHECK(loaded.profiles[i].eagerness ==
				profiles[25 - identity].eagerness);
			CHECK(loaded.profiles[i].colors[1] ==
				profiles[25 - identity].colors[1]);
		}
	}

	free(text);
	sentinel = (unsigned char *)profiles;
	text = sentinel;
	size = 123;
	strcpy(profiles[0].horn, "//bad");
	CHECK(mh_league_save_text(&league, profiles, 26, "Test", 0, 0, 0, &text,
		&size, NULL) != MH_ERROR_NONE);
	CHECK(text == sentinel && size == 123);
}

static void
league_schedule(void)
{
	static const unsigned long divisions[8] = {
		101, 310, 200, 201, 400, 301, 300, 100};

	static const unsigned int starts[4] = {0, 2, 10, 18};
	static const unsigned int counts[4] = {1, 6, 4, 2};
	struct mh_league league;
	struct mh_league_schedule schedule, saved;
	unsigned int group;

	for (group = 0; group < 4; group++) {
		memset(&league, 0, sizeof(league));
		league.entries[starts[group]].human = 1;
		CHECK(mh_league_schedule(&league, divisions, 8, &schedule,
			NULL) == MH_ERROR_NONE);
		CHECK(schedule.stage == group &&
			schedule.count == counts[group]);
		CHECK(!schedule.complete && schedule.outcome == 0 &&
			schedule.track_index == (group == 0 ? 4U : 0U));
		league.race_index = counts[group] - 1;
		CHECK(mh_league_schedule(&league, divisions, 8, &schedule,
			NULL) == MH_ERROR_NONE);
		CHECK(!schedule.complete &&
			schedule.track_index == (group == 0 ? 4U : 7U));
		league.race_index++;
		CHECK(mh_league_schedule(&league, divisions, 8, &schedule,
			NULL) == MH_ERROR_NONE);
		CHECK(schedule.complete && schedule.track_index == 8 &&
			schedule.outcome == 4 - group);
	}

	saved = schedule;
	CHECK(mh_league_schedule(&league, NULL, 8, &schedule, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&schedule, &saved, sizeof(schedule)) == 0);
	CHECK(mh_league_schedule(&league, NULL, 0, &schedule, NULL) ==
			MH_ERROR_NONE &&
		schedule.complete && schedule.count == 0);
}

static void
league_preparation(void)
{
	static const unsigned int starts[4] = {0, 2, 10, 18};
	struct mh_league league, expected;
	unsigned long seed, reference_seed;
	unsigned int mask, group, i;

	for (mask = 0; mask < 16; mask++) {
		memset(&league, 0, sizeof(league));

		for (i = 0; i < 26; i++)
			league.entries[i].position = 99;

		for (group = 0; group < 4; group++)
			if (mask & (1U << group))
				league.entries[starts[group]].human = 1;

		expected = league;
		seed = reference_seed = 1;

		for (group = 0; group < 4; group++)
			if (!(mask & (1U << group)))
				CHECK(mh_league_simulate_group(&expected, group,
					&reference_seed,
					NULL) == MH_ERROR_NONE);

		CHECK(mh_league_prepare_positions(&league, &seed, NULL) ==
			MH_ERROR_NONE);
		CHECK(seed == reference_seed &&
			memcmp(&league, &expected, sizeof(league)) == 0);
	}
}

static void
league_simulation(void)
{
	static const unsigned int expected[8] = {7, 2, 4, 3, 5, 6, 8, 1};
	struct mh_league league, saved;
	unsigned long seed;
	unsigned int i;

	memset(&league, 0, sizeof(league));

	for (i = 0; i < 26; i++) {
		league.entries[i].driver = i;
		league.entries[i].points = 100 + i;
	}

	saved = league;
	seed = 1;
	CHECK(mh_league_simulate_group(&league, 2, &seed, NULL) ==
			MH_ERROR_NONE &&
		seed == 3834121963UL);

	for (i = 0; i < 26; i++) {
		CHECK(league.entries[i].position ==
			(i >= 10 && i < 18 ? expected[i - 10] : 0));
		CHECK(league.entries[i].driver == i &&
			league.entries[i].points == 100 + i);
	}

	saved = league;
	CHECK(mh_league_simulate_group(&league, 4, &seed, NULL) !=
			MH_ERROR_NONE &&
		seed == 3834121963UL);
	CHECK(memcmp(&league, &saved, sizeof(league)) == 0);
	seed = 1;
	CHECK(mh_league_simulate_group(&league, 0, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(league.entries[0].position == 1 &&
		league.entries[1].position == 2);
}

static void
league_results(void)
{
	struct mh_league league, saved;
	struct mh_standings standings;
	unsigned int drivers[2], i;

	memset(&league, 0, sizeof(league));
	memset(&standings, 0, sizeof(standings));

	for (i = 0; i < 26; i++) {
		league.entries[i].driver = 25 - i;
		league.entries[i].position = 8;
	}

	standings.count = 2;
	standings.cars[0].position = 2;
	standings.cars[1].position = 1;
	standings.cars[0].estimated = 1;
	drivers[0] = 25;
	drivers[1] = 24;
	CHECK(mh_league_apply_results(&league, &standings, drivers, 3, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(league.entries[0].driver == 24 && league.entries[0].points == 12);
	CHECK(league.entries[1].driver == 25 && league.entries[1].points == 10);
	CHECK(!standings.cars[0].finished && standings.cars[0].estimated);
	saved = league;
	drivers[1] = 25;
	CHECK(mh_league_apply_results(&league, &standings, drivers, 3, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&league, &saved, sizeof(league)) == 0);
	drivers[1] = 99;
	CHECK(mh_league_apply_results(&league, &standings, drivers, 3, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&league, &saved, sizeof(league)) == 0);
}

static void
league_award(void)
{
	struct mh_league league, saved;
	unsigned int i;

	memset(&league, 0, sizeof(league));
	league.race_index = MH_U32_MAX;

	for (i = 0; i < 26; i++) {
		league.entries[i].driver = i;
		league.entries[i].position = 1;
		league.entries[i].points = 9;
		league.entries[i].wins = MH_U32_MAX;
		league.entries[i].human = 1;
	}

	CHECK(mh_league_award(&league, 0, NULL) == MH_ERROR_NONE &&
		league.race_index == 0);

	for (i = 0; i < 26; i++) {
		CHECK(league.entries[i].driver == i &&
			league.entries[i].wins == 0);
		CHECK(league.entries[i].points ==
			(i >= 2 && i < 18 ? 10UL : 21UL));
	}

	league.entries[2].human = 0;
	league.entries[2].points = MH_U32_MAX;
	CHECK(mh_league_award(&league, 1, NULL) == MH_ERROR_NONE);
	CHECK(league.entries[9].driver == 2 && league.entries[9].points == 11);
	league.entries[25].position = 0;
	saved = league;
	CHECK(mh_league_award(&league, 0, NULL) != MH_ERROR_NONE &&
		memcmp(&league, &saved, sizeof(league)) == 0);
}

static void
league_rollover(void)
{
	static const unsigned int expected[26] = {0, 2, 1, 3, 4, 5, 6, 7, 10,
		11, 8, 9, 12, 13, 14, 15, 18, 19, 16, 17, 20, 21, 22, 23, 24,
		25};

	struct mh_league league, saved;
	unsigned int i;

	memset(&league, 0, sizeof(league));
	league.race_index = 7;

	for (i = 0; i < 26; i++) {
		league.entries[i].driver = i;
		league.entries[i].human = i == 18;
		league.entries[i].position = i + 100;
		league.entries[i].points = MH_U32_MAX;
		league.entries[i].wins = league.entries[i].second_places =
			league.entries[i].third_places = i + 1;
	}

	CHECK(mh_league_rollover(&league, NULL) == MH_ERROR_NONE &&
		league.race_index == 0);

	for (i = 0; i < 26; i++) {
		CHECK(league.entries[i].driver == expected[i]);
		CHECK(league.entries[i].position == expected[i] + 100);
		CHECK(league.entries[i].human == (expected[i] == 18));
		CHECK(league.entries[i].points == 0 &&
			league.entries[i].wins == 0 &&
			league.entries[i].second_places == 0 &&
			league.entries[i].third_places == 0);
	}

	saved = league;
	CHECK(mh_league_rollover(NULL, NULL) != MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	league.entries[25].third_places = MH_U32_MAX + 1UL;
	saved = league;
	CHECK(mh_league_rollover(&league, NULL) != MH_ERROR_NONE);
#endif
	CHECK(memcmp(&league, &saved, sizeof(league)) == 0);
}

static void
league_state(void)
{
	unsigned int masks[4] = {0, 0, 0, 0};
	unsigned int stage, outcome, group, rank;
	unsigned int expected;

	CHECK(mh_league_classify_groups(masks, &stage, &outcome, NULL) ==
		MH_ERROR_NONE);
	CHECK(stage == 3 && outcome == 5);

	for (group = 0; group < 4; group++)
		for (rank = 0; rank < (group == 0 ? 2U : 8U); rank++) {
			memset(masks, 0, sizeof(masks));
			masks[group] = 1U << rank;
			expected = group == 0 ? (rank == 0 ? 4 : 7)
				: group == 1  ? (rank == 0		    ? 3
								: rank >= 6 ? 6
									: 5)
				: group == 2  ? (rank < 2		    ? 2
								: rank >= 6 ? 6
									: 5)
					: (rank < 2 ? 1 : 5);

			CHECK(mh_league_classify_groups(masks, &stage, &outcome,
				NULL) == MH_ERROR_NONE);
			CHECK(stage == group && outcome == expected);
		}

	masks[0] = 3;
	masks[1] = masks[2] = masks[3] = 255;
	CHECK(mh_league_classify_groups(masks, &stage, &outcome, NULL) ==
			MH_ERROR_NONE &&
		stage == 0 && outcome == 4);
	masks[0] = 4;
	CHECK(mh_league_classify_groups(masks, &stage, &outcome, NULL) !=
			MH_ERROR_NONE &&
		stage == 0 && outcome == 4);
}

static void
league_order(void)
{
	unsigned long points[8] = {12, 24, 24, 0, MH_U32_MAX, MH_U32_SIGN_BIT,
		16777216UL, 16777217UL};

	unsigned int order[8], saved[8], i;
	static const unsigned int expected[8] = {4, 5, 7, 6, 1, 2, 0, 3};

	CHECK(mh_league_points_order(points, 8, order, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(order[i] == expected[i]);

	memcpy(saved, order, sizeof(order));
	CHECK(mh_league_points_order(points, 0, order, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(saved, order, sizeof(order)) == 0);
	CHECK(mh_league_points_order(points, 9, order, NULL) != MH_ERROR_NONE);
	CHECK(mh_league_points_order(NULL, 8, order, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, order, sizeof(order)) == 0);
}

static void
estimates(void)
{
	struct mh_result_estimate rows[3], saved[3];
	unsigned long seed;
	unsigned int i;

	memset(rows, 0, sizeof(rows));

	for (i = 0; i < 3; i++) {
		rows[i].active = 1;
		rows[i].position = i + 1;
		rows[i].lap = rows[i].lap_limit = 3;
		rows[i].best_ms = 30000;
		rows[i].total_ms = 90000;
	}

	rows[0].finished = 1;
	rows[0].lap = 4;
	rows[0].total_ms = 100000;
	rows[1].route_parameter = 25;
	rows[2].route_parameter = 75;
	seed = 1;
	CHECK(mh_result_estimate_times(rows, 3, 0, 100, &seed, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Finished times stay fixed while unfinished cars consume the random
	 * seed.
	 */
	CHECK(rows[0].total_ms == 100000 && rows[1].total_ms == 103338 &&
		rows[2].total_ms == 89258);
	CHECK(rows[1].best_ms == 30000 && rows[2].best_ms == 30000);
	CHECK(!rows[1].finished && !rows[2].finished && seed == 2524885223UL);

	for (i = 0; i < 3; i++) {
		rows[i].lap = rows[i].lap_limit = 1;
		rows[i].total_ms = i == 0 ? 100000 : 90000;
	}

	rows[0].lap = 2;
	seed = 1;
	CHECK(mh_result_estimate_times(rows, 3, 0, 100, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(rows[0].total_ms == 100000 && rows[1].total_ms == 103338 &&
		rows[2].total_ms == 107592);
	CHECK(rows[0].best_ms == 30000 && rows[1].best_ms == 103338 &&
		rows[2].best_ms == 107592 && seed == 2524885223UL);
	memcpy(saved, rows, sizeof(rows));
	seed = 1;
	CHECK(mh_result_estimate_times(rows, 3, 4, 0, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);

	rows[2].route_parameter = 101;
	memcpy(saved, rows, sizeof(rows));
	CHECK(mh_result_estimate_times(rows, 3, 0, 100, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
	rows[2].route_parameter = 75;
	rows[0].position = 0;
	memcpy(saved, rows, sizeof(rows));
	CHECK(mh_result_estimate_times(rows, 3, 0, 100, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
}

static void
abort_positions(void)
{
	struct mh_result_estimate rows[8], saved[8];
	unsigned int i, seen;
	unsigned long seed;

	memset(rows, 0, sizeof(rows));

	for (i = 0; i < 8; i++) {
		rows[i].active = 1;
		rows[i].position = i + 1;
	}

	memcpy(saved, rows, sizeof(rows));
	seed = 1;
	CHECK(mh_result_abort_positions(rows, 8, 0, 0, 0, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
	CHECK(mh_result_abort_positions(rows, 8, 0, 4, 1, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
	CHECK(mh_result_abort_positions(rows, 8, 0, 0, 1, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(rows[0].position == 8 && seed != 1);
	seen = 0;

	for (i = 0; i < 8; i++) {
		CHECK(rows[i].position >= 1 && rows[i].position <= 8);
		seen |= 1U << rows[i].position;
		saved[i].position = rows[i].position;
	}

	CHECK(seen == 510 && memcmp(rows, saved, sizeof(rows)) == 0);
	rows[2].active = 2;
	memcpy(saved, rows, sizeof(rows));
	seed = 1;
	CHECK(mh_result_abort_positions(rows, 8, 0, 0, 1, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
	CHECK(mh_result_abort_positions(rows, 0, 0, 0, 1, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_result_abort_positions(rows, 8, 8, 0, 1, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_result_abort_positions(rows, 8, 0, 0, 2, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(rows, saved, sizeof(rows)) == 0);
}

static void
rankings(void)
{
	struct mh_standing_sample cars[8];
	struct mh_standings standings, saved;
	unsigned int i;

	memset(cars, 0, sizeof(cars));

	for (i = 0; i < 8; i++) {
		cars[i].score = 100;
		cars[i].best_ms = 1000000;
	}

	CHECK(mh_standings_rank(cars, 8, &standings, NULL) == MH_ERROR_NONE);
	CHECK(!standings.complete && standings.finished_count == 0);

	for (i = 0; i < 8; i++) {
		CHECK(standings.order[i] == i &&
			standings.cars[i].position == i + 1);
		CHECK(standings.cars[i].finish_ms == 0 &&
			standings.cars[i].best_ms == 0);
	}

	/*
	 * Swap partition retains finishers' order but moves entry zero behind
	 * two.
	 */
	cars[1].finished = cars[3].finished = 1;
	cars[1].total_ms = cars[3].total_ms = 2000;
	cars[1].best_ms = 1000;
	cars[1].best_lap = 2;
	CHECK(mh_standings_rank(cars, 4, &standings, NULL) == MH_ERROR_NONE);
	CHECK(standings.order[0] == 1 && standings.order[1] == 3 &&
		standings.order[2] == 2 && standings.order[3] == 0);
	CHECK(standings.finished_count == 2 && !standings.complete);
	CHECK(standings.cars[1].best_ms == 1000 &&
		standings.cars[1].best_lap == 2);

	/* Better unfinished progress cannot beat a finisher. */
	cars[0].score = MH_U32_MAX;
	cars[0].total_ms = 4000;
	cars[3].total_ms = 1999;
	CHECK(mh_standings_rank(cars, 4, &standings, NULL) == MH_ERROR_NONE);
	CHECK(standings.order[0] == 3 && standings.order[1] == 1 &&
		standings.order[2] == 0 && standings.order[3] == 2);
	CHECK(standings.cars[1].gap_ms == 1 &&
		standings.cars[0].finish_ms == 0 &&
		standings.cars[0].gap_ms == 0);

	/*
	 * Exact time/score differences lost in binary32 stay tied in roster
	 * order.
	 */
	memset(cars, 0, sizeof(cars));
	cars[0].score = 16777216UL;
	cars[1].score = 16777217UL;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) == MH_ERROR_NONE &&
		standings.order[0] == 0);
	cars[0].finished = cars[1].finished = 1;
	cars[0].total_ms = 16777217UL;
	cars[1].total_ms = 16777216UL;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) == MH_ERROR_NONE);
	CHECK(standings.complete && standings.finished_count == 2 &&
		standings.order[0] == 0);
	CHECK(standings.cars[1].gap_ms == -1);
	cars[1].total_ms = (unsigned long)MH_I32_MAX;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) == MH_ERROR_NONE);
	CHECK(standings.cars[1].gap_ms == 2130706430L);

	/* A recorded lap can be detected before its saved timestamp. */
	cars[0].finished = 0;
	cars[0].total_ms = 79591;
	cars[0].best_ms = 79600;
	cars[0].best_lap = 1;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) == MH_ERROR_NONE &&
		standings.cars[0].best_ms == 79600);
	saved = standings;
	CHECK(mh_standings_rank(cars, 0, &standings, NULL) != MH_ERROR_NONE);
	CHECK(mh_standings_rank(cars, MH_STANDINGS_MAX_CARS + 1, &standings,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_standings_rank(NULL, 1, &standings, NULL) != MH_ERROR_NONE);
	CHECK(mh_standings_rank(cars, 1, NULL, NULL) != MH_ERROR_NONE);
	cars[0].total_ms = MH_U32_SIGN_BIT;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) != MH_ERROR_NONE);
	cars[0].total_ms = 0;
	cars[0].best_ms = MH_U32_SIGN_BIT;
	cars[0].best_lap = 1;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) != MH_ERROR_NONE);
	cars[0].best_ms = 1;
	cars[0].best_lap = 26;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) != MH_ERROR_NONE);
	cars[0].best_lap = 0;
	cars[0].finished = 2;
	CHECK(mh_standings_rank(cars, 2, &standings, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&standings, &saved, sizeof(standings)) == 0);
	memset(cars, 0, sizeof(cars));

	for (i = 0; i < 4; i++) {
		cars[i].finished = 1;
		cars[i].total_ms = i * 1000;
	}

	cars[1].finished = 0;
	cars[1].score = 100;
	CHECK(mh_standings_rank_active(cars, 4, 14, &standings, NULL) ==
		MH_ERROR_NONE);
	CHECK(standings.finished_count == 2 && !standings.complete &&
		standings.cars[0].position == 0);
	CHECK(standings.order[0] == 2 && standings.order[1] == 3 &&
		standings.order[2] == 1 && standings.order[3] == 0);
	CHECK(standings.cars[2].position == 1 &&
		standings.cars[2].gap_ms == 0 &&
		standings.cars[3].gap_ms == 1000);
	CHECK(mh_standings_rank_active(cars, 4, 12, &standings, NULL) ==
			MH_ERROR_NONE &&
		standings.complete);
	CHECK(standings.order[2] == 0 && standings.order[3] == 1);
	saved = standings;
	CHECK(mh_standings_rank_active(cars, 4, 0, &standings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_standings_rank_active(cars, 4, 16, &standings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_standings_rank_active(cars, MH_STANDINGS_MAX_CARS + 1, 1,
		&standings, NULL) != MH_ERROR_NONE);
	CHECK(mh_standings_rank_active(NULL, 4, 1, &standings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_standings_rank_active(cars, 4, 1, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&standings, &saved, sizeof(standings)) == 0);
}

int
main(void)
{

	league_schedule();
	league_preparation();
	league_simulation();
	league_results();
	league_award();
	league_save();
	league_rollover();
	league_state();
	league_order();
	estimates();
	abort_positions();
	rankings();

	if (failures != 0)
		return 1;

	puts("Progress ordering, partition ties, actual results, binary32 keys and atomic bounds passed");

	return 0;
}
