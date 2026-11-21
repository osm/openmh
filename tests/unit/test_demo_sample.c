#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/sample.h"

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
	unsigned char older_body[30], newer_body[30];
	struct mh_demo_frame older, newer;
	struct mh_demo_sample sample, saved_sample;
	unsigned int slot;

	memset(older_body, 0, sizeof(older_body));
	memset(newer_body, 0, sizeof(newer_body));
	older_body[6] = 32;
	newer_body[6] = 64;
	older_body[9] = 255;
	older_body[10] = 31;
	older_body[11] = 255;
	newer_body[0] = 255;
	newer_body[5] = 100;
	memset(&older, 0, sizeof(older));
	memset(&newer, 0, sizeof(newer));
	older.milliseconds = 10;
	newer.milliseconds = 74;

	for (slot = 0; slot < 8; slot++) {
		older.vehicles[slot] = older_body;
		newer.vehicles[slot] = newer_body;
		older.active_mask = 1U << slot;
		older.unavailable_mask = 1U << slot;
		CHECK(mh_demo_sample_pair(&older, &newer, slot, 42, &sample,
			NULL) == MH_ERROR_NONE);
		CHECK(sample.active && sample.unavailable &&
			sample.vehicle.gear == 1);
		CHECK(sample.vehicle.rpm == 3125 &&
			sample.vehicle.extension[0] == 1);
		CHECK(sample.vehicle.codes[0] == 7 &&
			sample.vehicle.variants[0] == 3);
		CHECK(sample.world_velocity[0] == 0 &&
			sample.euler_rate[1] == 0);
	}

	memcpy(&saved_sample, &sample, sizeof(sample));
	CHECK(mh_demo_sample_pair(NULL, &newer, 0, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, NULL, 0, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 42, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, &newer, 8, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 9, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 74, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_sample_pair(&older, &newer, 0, MH_U32_MAX, &sample,
		NULL) != MH_ERROR_NONE);
	older.milliseconds = 74;
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 74, &sample, NULL) !=
		MH_ERROR_NONE);
	older.milliseconds = 75;
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 74, &sample, NULL) !=
		MH_ERROR_NONE);
	older.milliseconds = 10;
	newer.vehicles[0] = NULL;
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	newer.vehicles[0] = newer_body;
	newer_body[20] = 128;
	newer_body[21] = 127; /* Positive infinity. */
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	newer_body[20] = 0;
	newer_body[21] = 80; /* Finite but outside native world bounds. */
	CHECK(mh_demo_sample_pair(&older, &newer, 0, 42, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) == 0);
	puts("demo interpolation: slots, older discrete state, bounds and atomic rejection passed");

	return 0;
}
