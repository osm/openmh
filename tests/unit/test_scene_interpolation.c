#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/pose.h"
#include "engine/render/scene_interpolation.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "interpolation line %d: %s\n",         \
				__LINE__, #expression);                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field before, after, output, saved;

static void
yaw(double radians, double r[9])
{
	memset(r, 0, 9 * sizeof(*r));
	r[0] = r[8] = cos(radians);
	r[2] = -sin(radians);
	r[6] = sin(radians);
	r[4] = 1;
}

int
main(void)
{
	double a[9], b[9], r[9];
	unsigned int i, j;
	double dot;

	/* Across +/-180 degrees the midpoint is 180, not zero. */
	yaw(3.12413936106985, a);
	yaw(-3.12413936106985, b);
	CHECK(mh_pose_blend(a, b, .5, r, NULL) == MH_ERROR_NONE);
	CHECK(fabs(r[0] + 1) < 1e-12 && fabs(r[8] + 1) < 1e-12);
	CHECK(fabs(r[2]) < 1e-12);

	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++) {
			dot = r[3 * i] * r[3 * j] +
				r[3 * i + 1] * r[3 * j + 1] +
				r[3 * i + 2] * r[3 * j + 2];
			CHECK(fabs(dot - (i == j ? 1 : 0)) < 1e-12);
		}

	CHECK(mh_pose_blend(a, b, 0, r, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(a, r, sizeof(a)) == 0);
	CHECK(mh_pose_blend(a, b, 1, r, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(b, r, sizeof(b)) == 0);
	memset(a, 0, sizeof(a));
	CHECK(mh_pose_blend(a, b, .5, r, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(b, r, sizeof(b)) == 0);
	memset(&before, 0, sizeof(before));
	before.count = before.active_mask = 1;
	before.cars[0].frame.fixed_ticks = 1;
	yaw(0, before.cars[0].vehicle.motion.rotation);

	for (i = 0; i < 9; i++)
		before.cars[0].camera.rotation[i] =
			(float)before.cars[0].vehicle.motion.rotation[i];

	before.cars[0].wheels.angle[0] = 125;
	after = before;
	after.cars[0].frame.fixed_ticks = 2;
	after.cars[0].vehicle.motion.position[0] = 8;
	after.cars[0].camera.position[0] = 8;
	after.cars[0].vehicle.motion.extension[0] = 1;
	after.cars[0].wheels.angle[0] = 2;
	after.cars[0].race.now_ms = 123;
	after.ai_random = 987;
	saved = after;
	CHECK(mh_scene_interpolate(&before, &after, .25, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.cars[0].vehicle.motion.position[0] == 2);
	CHECK(output.cars[0].camera.position[0] == 2);
	CHECK(output.cars[0].vehicle.motion.extension[0] == .25);
	CHECK(fabs(output.cars[0].wheels.angle[0] -
		(125 + .25 * (2 + 125.6636962890625 - 125))) < 1e-12);
	CHECK(output.ai_random == 987 && output.cars[0].race.now_ms == 123);
	CHECK(memcmp(&after, &saved, sizeof(after)) == 0);
	after.cars[0].recovery_event = MH_RECOVERY_SUPPORT;
	CHECK(mh_scene_interpolate(&before, &after, .25, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.cars[0].vehicle.motion.position[0] == 8);
	after.cars[0].recovery_event = MH_RECOVERY_NONE;
	after.cars[0].vehicle.motion.position[0] = 100;
	CHECK(mh_scene_interpolate(&before, &after, .25, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.cars[0].vehicle.motion.position[0] == 100);
	after = saved;
	after.cars[0].frame.fixed_ticks = 0;
	CHECK(mh_scene_interpolate(&before, &after, .25, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.cars[0].vehicle.motion.position[0] == 8);
	CHECK(mh_scene_interpolate(&before, &after, .25, &after, NULL) ==
		MH_ERROR_INVALID);
	puts("presentation interpolation: short rotation arc, orthonormality, attachments, snap and isolation passed");

	return 0;
}
