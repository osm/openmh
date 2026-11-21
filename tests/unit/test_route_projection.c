#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/route.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/simulation/route_projection.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
rounding_cycle(void)
{
	struct mh_route route;
	struct mh_scene_key keys[5];
	struct mh_route_projection projection;
	double position[3];
	unsigned int i;

	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 5;

	for (i = 0; i < 5; i++) {
		keys[i].time = i * 10;
		keys[i].linear = 1;
		keys[i].value[1] = 2.175535;
	}

	/* Redrock neighbors; both distances round upward to 280.601196. */
	keys[0].value[0] = -2.931384;
	keys[0].value[2] = 30.75951;
	keys[1].value[0] = -1.031399;
	keys[1].value[2] = 63.99131;
	keys[2].value[0] = keys[2].value[2] = 1000;
	keys[3].value[0] = -1.323989;
	keys[3].value[2] = 16.01858;
	keys[4].value[0] = keys[0].value[0];
	keys[4].value[2] = keys[0].value[2];
	position[0] = -0.16300554146840407;
	position[1] = 2.717034043228333;
	position[2] = 47.27144614473424;

	for (i = 0; i < 2; i++) {
		CHECK(mh_route_project_cached(&route, position, i, &projection,
			NULL) == MH_ERROR_NONE &&
			projection.nearest_key == 0 &&
			projection.parameter > 4.999 &&
			projection.parameter < 5.001);
	}
}

int
main(void)
{
	struct mh_route route;
	struct mh_scene_key keys[5];
	struct mh_route_projection projection, saved;
	double position[3];
	unsigned int i;

	rounding_cycle();
	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 5;

	for (i = 0; i < 5; i++) {
		keys[i].time = i * 10;
		keys[i].linear = 1;
	}

	keys[1].value[0] = keys[2].value[0] = 10;
	keys[2].value[2] = keys[3].value[2] = 10;
	position[0] = 5;
	position[1] = position[2] = 0;
	CHECK(mh_route_project_cached(&route, position, MH_ROUTE_CACHE_EMPTY,
		&projection, NULL) == MH_ERROR_NONE &&
		projection.nearest_key == 0);
	CHECK(mh_route_project_cached(&route, position, 0, &projection, NULL) ==
			MH_ERROR_NONE &&
		projection.nearest_key == 1 && projection.parameter == 5);
	CHECK(mh_route_project_cached(&route, position, 12, &projection,
		NULL) == MH_ERROR_NONE &&
		projection.nearest_key == 1);
	position[0] = 0;
	position[2] = 5;
	CHECK(mh_route_project_cached(&route, position, 3, &projection, NULL) ==
			MH_ERROR_NONE &&
		projection.parameter == 35 && projection.position[2] == 10);
	saved = projection;
	position[1] = 1e9;
	CHECK(mh_route_project_cached(&route, position, 0, &projection, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&projection, &saved, sizeof(projection)) == 0);
	position[1] = 0;

	for (i = 0; i < 5; i++)
		memset(keys[i].value, 0, sizeof(keys[i].value));

	CHECK(mh_route_project_cached(&route, position, 0, &projection, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&projection, &saved, sizeof(projection)) == 0);
	route.motion.key_count = 3;
	CHECK(mh_route_project_cached(&route, position, 0, &projection, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&projection, &saved, sizeof(projection)) == 0);

	return failures != 0;
}
