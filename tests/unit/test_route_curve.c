#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/route.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/simulation/race.h"
#include "engine/simulation/route_curve.h"

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
	struct mh_route route;
	struct mh_scene_key keys[4];
	float value[9], saved[9], plane[4];
	unsigned int i;

	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 4;
	route.motion.end_behavior = 2;

	for (i = 0; i < 4; i++) {
		keys[i].time = i;
		keys[i].linear = 1;
	}

	keys[1].value[0] = 10;
	keys[2].value[2] = 10;
	CHECK(mh_route_curve_sample(&route, .5, value, NULL) == MH_ERROR_NONE &&
		value[0] == 5 && value[2] == 0);
	CHECK(mh_route_curve_sample(&route, -.5, value, NULL) ==
			MH_ERROR_NONE &&
		value[0] == 0 && value[2] == 5);
	CHECK(mh_route_curve_sample(&route, 3.5, value, NULL) ==
			MH_ERROR_NONE &&
		value[0] == 5 && value[2] == 0);
	CHECK(mh_route_finish_plane(&route, plane, NULL) == MH_ERROR_NONE &&
		plane[0] > .7f && plane[2] < -.7f && plane[3] == 0);
	memcpy(saved, value, sizeof(saved));
	CHECK(mh_route_curve_sample(&route, 1e10, value, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, value, sizeof(value)) == 0);

	for (i = 0; i < 4; i++)
		memset(keys[i].value, 0, sizeof(keys[i].value));

	CHECK(mh_route_curve_direction(&route, 0, value, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, value, sizeof(value)) == 0);

	return failures != 0;
}
