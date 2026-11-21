#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ai_path.h"
#include "engine/core/error.h"
#include "engine/simulation/ai_roster.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_history(void)
{
	struct mh_ai_path path;
	struct mh_ai_point points[128];
	struct mh_ai_section section;
	struct mh_ai_roster history, saved;
	struct mh_ai_roster_driver drivers[8];
	struct mh_ai_roster_result result, before;
	unsigned int i;

	memset(points, 0, sizeof(points));
	memset(&section, 0, sizeof(section));
	memset(drivers, 0, sizeof(drivers));
	section.count = 128;

	for (i = 0; i < 128; i++) {
		points[i].z = i;
		points[i].direction[2] = 1;
		points[i].width[0] = points[i].width[1] = 5;
	}

	path.points = points;
	path.count = 128;
	path.sections = &section;
	path.section_count = 1;

	for (i = 0; i < 3; i++) {
		drivers[i].ai = 1;
		drivers[i].personality_enabled = 1;
		drivers[i].config.mass = 1000;
		drivers[i].config.corner_grip = 2;
		drivers[i].config.corner_steering = 14;
		drivers[i].feedback.x = (double)i - 1;
		drivers[i].feedback.z = 10 + i;
		drivers[i].feedback.heading_z = 1;
		drivers[i].feedback.speed = 5;
		CHECK(mh_ai_driver_start(&path, drivers[i].feedback.x,
			drivers[i].feedback.z, &drivers[i].state,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_ai_personality_start(drivers[i].parameters,
			&drivers[i].personality, NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_ai_roster_start(&path, drivers, 7, &history, NULL) ==
		MH_ERROR_NONE);
	CHECK(history.cars[0].lateral == 1 && history.cars[0].speed == 0 &&
		history.cars[0].heading_dot == 1);
	drivers[0].feedback.z = 60;
	saved = history;
	CHECK(mh_ai_roster_step(&path, &history, drivers, 1, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&history, &saved, sizeof(history)) == 0);

	/*
	 * Patched sorting visits all active rows despite trailing inactive
	 * slots. It still uses the old point, before slot zero moves to
	 * point 60.
	 */
	CHECK(result.history.order[0] == 2 && result.history.order[1] == 1 &&
		result.history.order[2] == 0);
	CHECK(result.history.cars[0].point == 60 &&
		result.history.cars[0].lateral == -1);
	CHECK(result.random != 1);
	history = result.history;

	for (i = 0; i < 3; i++) {
		drivers[i].state = result.drivers[i].state;
		drivers[i].personality = result.personality[i];
	}

	CHECK(mh_ai_roster_step(&path, &history, drivers, result.random, .01,
		&result, NULL) == MH_ERROR_NONE);
	CHECK(result.history.order[0] == 0 && result.history.order[1] == 2 &&
		result.history.order[2] == 1);
	before = result;
	drivers[0].config.mass = 0;
	CHECK(mh_ai_roster_step(&path, &history, drivers, result.random, .01,
		&result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &result, sizeof(result)) == 0);
	history.order[1] = history.order[0];
	CHECK(mh_ai_roster_step(&path, &history, drivers, 1, .01, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &result, sizeof(result)) == 0);
	drivers[0].ai = 0;
	CHECK(mh_ai_driver_start(&path, -1, 60, &drivers[0].state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_ai_roster_start(&path, drivers, 1, &history, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_ai_roster_step(&path, &history, drivers, 123, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.random == 123 && result.history.cars[0].lane == 1 &&
		result.history.cars[0].lateral == -1);
}

int
main(void)
{

	check_history();

	if (failures != 0)
		return 1;

	puts("Retained roster sorting, publication, human RNG and atomic failure checks passed");

	return 0;
}
