#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/simulation/pair.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-9)

int
main(void)
{
	unsigned char bytes[278];
	struct mh_collision hull;
	struct mh_car car_a, car_b;
	struct mh_motion_state motion_a, motion_b;
	struct mh_pair_result result, saved;
	struct mh_contact constraints[7];
	double position[3] = {0, 0, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int i;

	CHECK(mh_collision_col_decode(bytes, pair_fixture(bytes), &hull,
		NULL) == MH_ERROR_NONE);
	memset(&car_a, 0, sizeof(car_a));
	car_a.has_corners = car_a.has_suspension = 1;
	car_a.mass = 1000;

	for (i = 0; i < 4; i++) {
		car_a.corners[i][0] = i == 0 || i == 3 ? -1 : 1;

		/* Replaced by COM Y; collinear arms give zero torque. */
		car_a.corners[i][1] = 99;
	}

	car_b = car_a;
	CHECK(mh_motion_reset(position, rotation, &motion_b, NULL) ==
		MH_ERROR_NONE);
	motion_a = motion_b;
	motion_a.position[0] = 1.5;
	motion_a.velocity[0] = -10;
	motion_a.previous_velocity[0] = 123;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.contact.hit && result.contact.source == 1 &&
		result.contact.corner == 1);
	NEAR(result.contact.depth, -.5);
	NEAR(result.impulse, 10000);
	NEAR(result.sound_severity, 20);
	NEAR(result.state[0].position[0], 1.8);
	NEAR(result.state[1].position[0], -.3);
	NEAR(result.state[0].velocity[0], 0);
	NEAR(result.state[1].velocity[0], -10);
	CHECK(result.state[0].previous_velocity[0] == 123);
	NEAR(result.angular_delta[0][1], 0);
	car_a.mass = 500;
	car_b.mass = 1500;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.state[0].velocity[0], 5);
	NEAR(result.state[1].velocity[0], -5);
	NEAR(car_a.mass * result.velocity_delta[0][0] +
			car_b.mass * result.velocity_delta[1][0],
		0);
	CHECK(mh_pair_step_remote(&car_a, &hull, &motion_a, &car_b, &hull,
		&motion_b, NULL, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.impulse, 7500);
	NEAR(result.sound_severity, 20);
	NEAR(result.state[0].position[0], 2.1);
	NEAR(result.state[0].velocity[0], 5);
	CHECK(memcmp(&result.state[1], &motion_b, sizeof(motion_b)) == 0);
	NEAR(result.velocity_delta[1][0], 0);
	NEAR(result.angular_delta[1][0], 0);

	/*
	 * Original response also acts on separating overlap; no approach guard.
	 */
	motion_a.velocity[0] = 10;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.impulse, -7500);
	NEAR(result.sound_severity, -20);
	motion_a.velocity[0] = -10;

	/*
	 * A wall normal +X blocks B's correction, but not the positive normal
	 * impulse.
	 */
	memset(constraints, 0, sizeof(constraints));
	constraints[0].hit = 1;
	constraints[0].normal[0] = 1;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		constraints, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.correction[0], 0);
	NEAR(result.state[1].position[0], 0);
	NEAR(result.state[1].velocity[0], -5);
	constraints[0].normal[0] = -1;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		constraints, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.state[1].velocity[0], 0);
	NEAR(result.correction[0], -.6);

	/* Corner lever arm produces fixed-inertia yaw reduced by 0.1. */
	car_a.mass = car_b.mass = 1000;

	for (i = 0; i < 4; i++)
		car_a.corners[i][2] = car_b.corners[i][2] = i < 2 ? 2 : -2;

	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	NEAR(result.impulse, 20 / .006);
	NEAR(result.angular_delta[1][1], -1.0 / 3);

	/* Misses preserve BOTH motions, and publish no stale diagnostics. */
	motion_a.position[0] = 10;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.contact.hit && result.impulse == 0 &&
		result.sound_severity == 0);
	CHECK(memcmp(&result.state[0], &motion_a, sizeof(motion_a)) == 0);
	motion_a.position[0] = 1.5;
	motion_a.position[1] = 1.1;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.contact.hit);
	CHECK(mh_pair_step_points(&car_a, &hull, &motion_a, 4, &car_b, &hull,
		&motion_b, 7, NULL, 0, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.contact.hit && result.contact.source == 1 &&
		result.contact.corner >= 4);
	memset(constraints, 0, sizeof(constraints));
	constraints[6].hit = 1;
	constraints[6].normal[1] = 1;
	CHECK(mh_pair_step_points(&car_a, &hull, &motion_a, 4, &car_b, &hull,
		&motion_b, 7, constraints, 0, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.correction[1], 0);

	/*
	 * Perpendicular boxes intersect, but no configured corner is contained.
	 */
	motion_a.position[0] = motion_a.position[1] = 0;
	motion_a.rotation[0] = motion_a.rotation[8] = 0;
	motion_a.rotation[2] = -1;
	motion_a.rotation[6] = 1;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.contact.hit);

	/* Degenerate center direction is deterministic and finite. */
	motion_a = motion_b;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.contact.hit);
	result.state[0].position[0] = 1.5;
	CHECK(mh_pair_step(&car_a, &hull, &result.state[0], &car_b, &hull,
		&result.state[1], NULL, &result, NULL) == MH_ERROR_NONE);
	saved = result;
	car_a.mass = 0;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		NULL, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	car_a.mass = 1000;
	constraints[0].hit = 1;
	constraints[0].normal[0] = 2;
	CHECK(mh_pair_step(&car_a, &hull, &motion_a, &car_b, &hull, &motion_b,
		constraints, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);

	mh_collision_free(&hull);

	if (failures)
		return 1;

	puts("Car pair impulses, mass, torque, constraints, geometric limits and rollback passed");

	return 0;
}
