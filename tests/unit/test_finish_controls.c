#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/finish_controls.h"

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
	struct mh_engine_input original, controls;
	unsigned int i;
	int finished, active;

	memset(&original, 0, sizeof(original));
	original.throttle = .8;
	original.brake = .6;
	original.steering = -.2;
	original.shift_up = original.shift_down = original.handbrake = 1;

	for (active = 0; active < 2; active++)
		for (finished = 0; finished < 2; finished++)
			for (i = 0; i < 3; i++) {
				controls = original;
				CHECK(mh_finish_controls(active, finished, i,
					10, .4f, &controls,
					NULL) == MH_ERROR_NONE);

				if (!active || !finished)
					CHECK(memcmp(&controls, &original,
						sizeof(controls)) == 0);
				else {
					CHECK(controls.throttle == 0 &&
						controls.brake == (double).1f);
					CHECK(controls.steering ==
						(i == 2 ? (double).4f
							: original.steering));
					CHECK(controls.shift_up ==
						(i == 2 ? 0 : 1));
					CHECK(controls.shift_down ==
						(i == 2 ? 0 : 1));
					CHECK(controls.handbrake ==
						(i == 2 ? 0 : 1));
				}
			}

	controls = original;
	CHECK(mh_finish_controls(1, 1, 2, 0, -.3f, &controls, NULL) ==
		MH_ERROR_NONE);
	CHECK(controls.throttle == (double).05f && controls.brake == 0 &&
		controls.steering == -.3f);
	CHECK(mh_finish_controls(1, 1, 1, -.001f, 0, &controls, NULL) ==
			MH_ERROR_NONE &&
		controls.throttle == (double).05f);
	controls = original;
	CHECK(mh_finish_controls(1, 1, 2, 1, 1e20f, &controls, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&controls, &original, sizeof(controls)) == 0);
	CHECK(mh_finish_controls(1, 1, 2, 1e20f, 0, &controls, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&controls, &original, sizeof(controls)) == 0);
	CHECK(mh_finish_controls(2, 1, 1, 0, 0, &controls, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_finish_controls(1, 2, 1, 0, 0, &controls, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_finish_controls(1, 1, 1, 0, 0, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&controls, &original, sizeof(controls)) == 0);

	if (failures == 0)
		puts("Finished-car control gates, constants, role policy and atomic validation passed");

	return failures != 0;
}
