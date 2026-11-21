#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/race.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_scores(const struct mh_race *initial)
{
	struct mh_race race, saved;
	struct mh_scene_key *keys;
	unsigned long score;

	race = *initial;
	keys = race.route->motion.keys;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 30720);
	saved = race;
	race.parameter = 30.75;
	race.visited[0] = 2;
	race.covered = 99;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 34590);
	race.previous_position[2] = -1;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 30750);
	race.visited[1] = 1;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 38430);

	/*
	 * Binary32 .7 is below .7: the original side is negative even though
	 * a double dot product at this point would round to exactly zero.
	 */
	race = saved;
	race.normal[0] = .7;
	race.normal[1] = race.normal[2] = 0;
	race.previous_position[0] = 10;
	race.previous_position[1] = race.previous_position[2] = 0;
	race.plane = -7;
	race.parameter = 30.75;
	race.visited[0] = 1;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 30750);
	race.parameter = 30.99999999;
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 30751);
	race.parameter = -1;
	score = 123;
	CHECK(mh_race_score(&race, &score, NULL) != MH_ERROR_NONE &&
		score == 123);
	race = saved;
	keys[64].time = 1e9;
	CHECK(mh_race_score(&race, &score, NULL) != MH_ERROR_NONE &&
		score == 123);
	keys[64].time = 1920;
	CHECK(mh_race_score(NULL, &score, NULL) != MH_ERROR_NONE &&
		score == 123);
	CHECK(mh_race_score(&race, NULL, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	struct mh_route route;
	struct mh_scene_key keys[65];
	struct mh_race race, saved;
	double angle;
	unsigned int i;
	unsigned long score;

	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 65;
	route.motion.end_behavior = 2;

	for (i = 0; i < 64; i++) {
		angle = i * 6.283185307179586 / 64;
		keys[i].time = i * 30;
		keys[i].linear = 1;
		keys[i].value[0] = 100 * (1 - cos(angle));
		keys[i].value[2] = 100 * sin(angle);
		keys[i].value[3] = i / 32.0;
	}

	keys[64] = keys[0];
	keys[64].time = 1920;
	keys[20].value[4] = 1;
	keys[40].value[4] = 2;
	CHECK(mh_race_start(&route, 2, keys[0].value, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.groups == 4 && race.lap == 1 && race.covered == 0 &&
		race.best_lap == 0);
	CHECK(race.split_hold_ms == 4000 && race.split_notice == 0 &&
		race.lap_notice == 0);
	CHECK(race.sound_blend == 0);
	check_scores(&race);

	for (i = 1; i <= 128; i++) {
		CHECK(mh_race_step(&race, keys[i % 64].value, i * 1000UL, &race,
			NULL) == MH_ERROR_NONE);

		if (i == 20)
			CHECK(race.split_event == 2 &&
				race.splits[1][2] == 20000);

		if (i >= 20 && i < 24)
			CHECK(race.split_notice == 2 && race.split_ms == 20000);

		if (i == 24)
			CHECK(race.split_notice == 0 && race.split_event == 0);

		if (i == 40)
			CHECK(race.split_event == 3 &&
				race.splits[1][3] == 40000);

		if (i == 63)
			CHECK(race.lap == 1 && race.covered == 4);

		if (i == 64)
			CHECK(race.lap == 2 && race.lap_event &&
				race.last_ms == 64000 && race.covered == 0);

		if (i == 64)
			CHECK(race.lap_ms == 64000 && race.split_ms == 0 &&
				race.split_notice == 1 &&
				race.lap_notice == 1 && race.race_notice == 0);

		if (i == 65)
			CHECK(race.lap_ms == 1000 && race.split_notice == 1 &&
				!race.split_event);

		if (i == 68)
			CHECK(race.split_notice == 0 && race.lap_notice == 1);
	}

	CHECK(race.finished && race.lap == 2 && race.last_ms == 64000 &&
		race.best_ms == 64000);
	CHECK(race.best_lap == 1 && race.splits[3][0] == 128000);

	/*
	 * Raw environment continues through the finished gate, while clocks,
	 * scoring parameter and completed lap data remain frozen.
	 */
	saved = race;
	CHECK(mh_race_step(&race, keys[48].value, race.now_ms, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.sound_blend == 1.5f && race.now_ms == saved.now_ms &&
		race.parameter == saved.parameter &&
		race.lap_ms == saved.lap_ms);
	CHECK(mh_race_rebase(&race, keys[0].value, &race, NULL) ==
			MH_ERROR_NONE &&
		race.sound_blend == 1.5f);
	race = saved;
	CHECK(race.lap_notice == 1 && race.race_notice == 1 &&
		race.lap_ms == 64000);
	CHECK(mh_race_score(&race, &score, NULL) == MH_ERROR_NONE &&
		score == 2147355647UL);
	saved = race;
	CHECK(mh_race_step(&race, keys[5].value, 130000, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.now_ms == saved.now_ms && race.last_ms == saved.last_ms &&
		!race.lap_event);
	CHECK(race.lap_ms == saved.lap_ms &&
		race.split_notice == saved.split_notice &&
		race.race_notice == 1 && race.lap_notice == 1);

	/* Exact hold boundary, pause, recovery and a changed lap origin. */
	CHECK(mh_race_start(&route, 2, keys[19].value, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[20].value, 20000, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 2);
	CHECK(mh_race_step(&race, keys[20].value, 23999, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 2);
	saved = race;
	CHECK(mh_race_step(&race, keys[20].value, 23999, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 2);
	CHECK(mh_race_rebase(&race, keys[21].value, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 2);
	CHECK(mh_race_step(&race, keys[21].value, 24000, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 0);
	race = saved;
	race.lap_start_ms = 21000;
	CHECK(mh_race_step(&race, keys[20].value, 24000, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 0);
	race = saved;
	race.split_hold_ms = 10000;
	CHECK(mh_race_step(&race, keys[20].value, 29999, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 2);
	CHECK(mh_race_step(&race, keys[20].value, 30000, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 0);
	race = saved;
	race.split_hold_ms = 0;
	CHECK(mh_race_step(&race, keys[20].value, 23999, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_notice == 0);

	/*
	 * Slower laps announce LAST, while a one-lap finish still announces
	 * both.
	 */
	CHECK(mh_race_start(&route, 2, keys[0].value, &race, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= 128; i++)
		CHECK(mh_race_step(&race, keys[i % 64].value,
			i <= 64 ? i * 1000UL : 64000 + (i - 64) * 1100UL,
			&race, NULL) == MH_ERROR_NONE);

	CHECK(race.finished && race.best_lap == 1 && race.lap_notice == 0 &&
		race.race_notice == 1);
	CHECK(mh_race_start(&route, 1, keys[0].value, &race, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= 64; i++)
		CHECK(mh_race_step(&race, keys[i % 64].value, i * 1000UL, &race,
			NULL) == MH_ERROR_NONE);

	CHECK(race.finished && race.lap == 1 && race.lap_notice == 1 &&
		race.race_notice == 1);

	/* Highest supported lap count writes the final boundary row at 26. */
	CHECK(mh_race_start(&route, 25, keys[0].value, &race, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= 25 * 64; i++)
		CHECK(mh_race_step(&race, keys[i % 64].value, i * 10UL, &race,
			NULL) == MH_ERROR_NONE);

	CHECK(race.finished && race.lap == 25 && race.splits[26][0] == 16000);

	/*
	 * Finish-line wiggles and a skipped middle section cannot earn a lap.
	 */
	CHECK(mh_race_start(&route, 1, keys[0].value, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[1].value, 100, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[49].value, 200, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[63].value, 300, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[0].value, 400, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(!race.finished && !race.lap_event && race.best_lap == 0);

	/*
	 * The original accumulator triggers strictly above 2000 ms and caps at
	 * 2500; standing still consumes it just like forward movement.
	 */
	CHECK(mh_race_start(&route, 1, keys[20].value, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_step(&race, keys[19].value, 1000, &race, NULL) ==
			MH_ERROR_NONE &&
		!race.wrong_way);
	CHECK(mh_race_step(&race, keys[18].value, 2000, &race, NULL) ==
			MH_ERROR_NONE &&
		!race.wrong_way);
	CHECK(mh_race_step(&race, keys[17].value, 2001, &race, NULL) ==
			MH_ERROR_NONE &&
		race.wrong_way);
	CHECK(mh_race_step(&race, keys[16].value, 4000, &race, NULL) ==
			MH_ERROR_NONE &&
		race.backward_ms == 2500);
	CHECK(mh_race_step(&race, keys[16].value, 4500, &race, NULL) ==
			MH_ERROR_NONE &&
		!race.wrong_way);
	CHECK(race.backward_ms == 2000);
	saved = race;
	CHECK(mh_race_rebase(&race, keys[40].value, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.now_ms == saved.now_ms && race.covered == saved.covered &&
		race.split_event == 0);
	CHECK(mh_race_step(&race, keys[40].value, 4500, &race, NULL) ==
			MH_ERROR_NONE &&
		race.split_event == 0);
	saved = race;
	CHECK(mh_race_step(&race, keys[41].value, 4499, &race, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved, sizeof(race)) == 0);
	CHECK(mh_race_step(&race, keys[41].value, MH_U32_SIGN_BIT, &race,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved, sizeof(race)) == 0);
	CHECK(mh_race_start(&route, 26, keys[0].value, &race, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved, sizeof(race)) == 0);
	keys[20].value[4] = 3;
	CHECK(mh_race_start(&route, 2, keys[0].value, &race, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved, sizeof(race)) == 0);
	route.motion.key_count = 31;
	CHECK(mh_race_start(&route, 2, keys[0].value, &race, NULL) !=
		MH_ERROR_NONE);

	if (failures != 0)
		return 1;

	puts("Lap coverage, checkpoint timing, reverse travel, recovery and clock bounds passed");

	return 0;
}
