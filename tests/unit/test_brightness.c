#include <float.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/brightness.h"

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
	double world[3] = {1, 2, 3}, object[3] = {2, 3, 4};
	float user[3] = {1, -1, -2};
	struct mh_brightness output, saved;

	CHECK(mh_brightness_prepare(world, object, user, 0, NULL, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.world[0] == 1 && output.world[1] == 0 &&
		output.world[2] == 0);
	CHECK(output.object[0] == 2 && output.object[1] == -3 &&
		output.object[2] == -8);
	CHECK(output.particle == (float)(1.0 / 3.0));
	CHECK(mh_brightness_prepare(world, object, user, 1, NULL, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.world[0] == .5 && output.object[1] == -1.5 &&
		output.object[2] == -4);
	CHECK(mh_brightness_prepare(world, object, user, 1, mh_water_style(),
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.world[0] == .5 && output.object[1] == 1 &&
		output.object[2] == (float)1.2);
	CHECK(output.particle == mh_water_style()->particle_brightness);
	CHECK(mh_brightness_prepare(world, object, NULL, 0, NULL, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.particle == 2 && output.object[2] == 4);
	saved = output;
	user[0] = 3;
	CHECK(mh_brightness_prepare(world, object, user, 0, NULL, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &output, sizeof(saved)) == 0);
	user[0] = -FLT_MAX;
	CHECK(mh_brightness_prepare(world, object, user, 0, NULL, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &output, sizeof(saved)) == 0);
	CHECK(mh_brightness_prepare(world, object, NULL, 2, NULL, &output,
		NULL) != MH_ERROR_NONE);

	return failures != 0;
}
