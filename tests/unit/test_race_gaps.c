#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/race_gaps.h"

static struct mh_race races[8];
static const struct mh_race *race_pointers[8];
static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
initialize_races(void)
{
	unsigned int i;

	memset(races, 0, sizeof(races));

	for (i = 0; i < 8; i++) {
		race_pointers[i] = races + i;
		races[i].lap = 1;
		races[i].split_notice = 2;
		races[i].split_hold_ms = 4000;
		races[i].now_ms = 10000 + i * 1000;
		races[i].splits[1][2] = 10000 + i * 1000;
	}
}

int
main(void)
{
	struct mh_race_gaps result, saved;
	unsigned int i;

	initialize_races();
	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 8);

	for (i = 0; i < 8; i++)
		CHECK(result.rows[i].entry == i &&
			result.rows[i].position == 0 &&
			result.rows[i].milliseconds == (long)i * 1000 - 3000);

	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 3);
	CHECK(result.rows[0].entry == 0 && result.rows[1].entry == 2 &&
		result.rows[2].entry == 4);
	CHECK(result.rows[0].position == 1 && result.rows[1].position == 3 &&
		result.rows[2].position == 5);
	CHECK(mh_race_gaps(race_pointers, 8, 255, 0, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 1 && result.rows[0].entry == 1);
	CHECK(mh_race_gaps(race_pointers, 8, 255, 7, 1, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 2);
	races[2].splits[1][2] = 0;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 7 && result.rows[2].entry == 3);
	CHECK(mh_race_gaps(race_pointers, 8, 253, 3, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 6 && result.rows[1].entry == 3);
	races[3].split_notice = 0;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 0);
	initialize_races();
	races[3].finished = 1;
	races[1].finished = 1;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 0, &result, NULL) ==
			MH_ERROR_NONE &&
		result.mode == 2 && result.count == 2);
	CHECK(result.rows[0].entry == 1 &&
		result.rows[0].milliseconds == -2000 &&
		result.rows[1].entry == 3);
	races[3].split_hold_ms = 0;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 3, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 0);
	initialize_races();
	races[0].splits[1][2] = 16777217;
	races[1].splits[1][2] = 16777216;
	CHECK(mh_race_gaps(race_pointers, 2, 3, 1, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.rows[0].entry == 0 && result.rows[0].milliseconds == 1);
	races[0].splits[1][2] = 99999996;
	CHECK(mh_race_gaps(race_pointers, 2, 3, 1, 2, &result, NULL) ==
			MH_ERROR_NONE &&
		result.count == 1);
	saved = result;
	CHECK(mh_race_gaps(race_pointers, MH_GAPS_MAX_CARS + 1, 255, 1, 2,
		&result, NULL) != MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);
	CHECK(mh_race_gaps(race_pointers, 8, 256, 1, 2, &result, NULL) !=
		MH_ERROR_NONE);
	races[1].split_notice = 4;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 1, 2, &result, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);
	initialize_races();
	races[0].splits[1][2] = MH_U32_SIGN_BIT;
	CHECK(mh_race_gaps(race_pointers, 8, 255, 1, 2, &result, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);

	return failures != 0;
}
