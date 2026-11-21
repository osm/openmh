#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/world_view.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_world_view view, saved;
	float pose[12], origin[3], spacing[2];
	unsigned long dimensions[2];

	memset(pose, 0, sizeof(pose));
	pose[0] = pose[4] = pose[8] = 1;
	origin[0] = origin[2] = -200;
	origin[1] = 0;
	spacing[0] = spacing[1] = 10;
	dimensions[0] = dimensions[1] = 100;
	CHECK(mh_world_view_setup(640, 480, 1, .5f, 120, pose, origin, spacing,
		dimensions, &view, NULL) == MH_ERROR_NONE);
	CHECK(view.first[0] == 7 && view.end[0] == 34 && view.first[1] == 19 &&
		view.end[1] == 34);
	CHECK(view.bounds[0] == -130 && view.bounds[1] == 130 &&
		view.bounds[2] == -9.5f && view.bounds[3] == 130);
	saved = view;
	spacing[0] = 0;
	CHECK(mh_world_view_setup(640, 480, 1, .5f, 120, pose, origin, spacing,
		dimensions, &view, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&view, &saved, sizeof(view)) == 0);
	spacing[0] = .5f;
	CHECK(mh_world_view_setup(640, 480, 1, .5f, 120, pose, origin, spacing,
		dimensions, &view, NULL) == MH_ERROR_NONE);
	CHECK(view.first[0] == 0 && view.end[0] == 0 && view.first[1] == 0 &&
		view.end[1] == 0);
	spacing[0] = 10;
	pose[9] = pose[11] = -10000;
	CHECK(mh_world_view_setup(640, 480, 1, .5f, 120, pose, origin, spacing,
		dimensions, &view, NULL) == MH_ERROR_NONE);
	CHECK(view.first[0] == 0 && view.end[0] == 0 && view.first[1] == 0 &&
		view.end[1] == 0);
	pose[9] = pose[11] = 10000;
	CHECK(mh_world_view_setup(640, 480, 1, .5f, 120, pose, origin, spacing,
		dimensions, &view, NULL) == MH_ERROR_NONE);
	CHECK(view.first[0] == 100 && view.end[0] == 100 &&
		view.first[1] == 100 && view.end[1] == 100);
	puts("world camera footprint and bounded grid selection passed");

	return 0;
}
