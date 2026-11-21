#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/frustum.h"
#include "engine/render/name_label.h"

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
	struct mh_frustum frustum, saved;
	float pose[12], center[3];

	memset(pose, 0, sizeof(pose));
	pose[0] = pose[4] = pose[8] = 1;
	CHECK(mh_frustum_setup(640, 480, 1, .5f, 120, pose, &frustum, NULL) ==
		MH_ERROR_NONE);
	center[0] = center[1] = 0;
	center[2] = .5f;
	CHECK(mh_frustum_sphere(&frustum, center, 0));
	center[2] = .499f;
	CHECK(!mh_frustum_sphere(&frustum, center, 0));
	CHECK(mh_frustum_sphere(&frustum, center, .01f));
	center[2] = 120;
	CHECK(mh_frustum_sphere(&frustum, center, 0));
	center[2] = 120.01f;
	CHECK(!mh_frustum_sphere(&frustum, center, 0));
	center[2] = 20;
	center[1] = 29;
	CHECK(mh_frustum_sphere(&frustum, center, 0));
	center[1] = 31;
	CHECK(!mh_frustum_sphere(&frustum, center, 0));
	CHECK(mh_frustum_sphere(&frustum, center, 1));
	saved = frustum;
	CHECK(mh_frustum_setup(0, 480, 1, .5f, 120, pose, &frustum, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frustum_setup(640, 480, 0, .5f, 120, pose, &frustum, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&frustum, &saved, sizeof(frustum)) == 0);
	CHECK(!mh_frustum_sphere(&frustum, center, -1));
	CHECK(mh_name_label_mask(8, 255, 0, 3, 0, 1) == 254);
	CHECK(mh_name_label_mask(8, 255, 0, 3, 1, 1) == 247);
	CHECK(mh_name_label_mask(3, 255, 0, 2, 0, 0) == 7);
	CHECK(mh_name_label_mask(MH_NAME_LABELS_MAX + 1, 255, 0, 0, 0, 0) == 0);
	CHECK(mh_name_label_mask(1, 255, 0, 1, 0, 0) == 0);
	puts("object frustum and name eligibility bounds passed");

	return 0;
}
