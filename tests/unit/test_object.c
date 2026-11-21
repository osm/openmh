#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/core/rotation.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/object.h"
#include "engine/simulation/pair.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_motion(void)
{
	struct mh_object_state state, next, saved;
	double position[3] = {0, 10, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	double damping;

	memset(&state, 0, sizeof(state));
	CHECK(mh_motion_reset(position, rotation, &state.motion, NULL) ==
		MH_ERROR_NONE);
	state.flags = 2;
	state.motion.velocity[0] = 10;
	CHECK(mh_object_integrate(&state, .1, &next, NULL) == MH_ERROR_NONE &&
		memcmp(&state, &next, sizeof(state)) == 0);
	state.flags = 1;
	state.motion.angular_velocity[0] = 1500;
	CHECK(mh_object_integrate(&state, .01, &next, NULL) == MH_ERROR_NONE &&
		next.motion.angular_velocity[0] > 1000);
	state.motion.angular_velocity[0] = 0;
	CHECK(mh_object_integrate(&state, .1, &next, NULL) == MH_ERROR_NONE);
	damping = pow((double)(float).9, .1);
	CHECK(fabs(next.motion.velocity[0] - 10 * damping) < 1e-12 &&
		next.flags == 1);
	CHECK(fabs(next.motion.position[0] - damping) < 1e-12);
	CHECK(fabs(next.motion.velocity[1] +
		(double)(float)15.81 * .087 * damping) < 1e-12);
	saved = next;
	CHECK(mh_object_integrate_gravity(&state, .1, 1, &next, NULL) ==
		MH_ERROR_NONE);
	CHECK(next.motion.velocity[1] == saved.motion.velocity[1] * .5);
	CHECK(next.motion.position[1] ==
		10 + (saved.motion.position[1] - 10) * .5);
	CHECK(next.motion.velocity[0] == saved.motion.velocity[0]);
	saved = next;
	CHECK(mh_object_integrate_gravity(&state, .1, 2, &next, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &next, sizeof(next)) == 0);
	state.contacts[7].hit = 1;
	CHECK(mh_object_integrate(&state, .1, &next, NULL) == MH_ERROR_NONE);
	CHECK(fabs(next.motion.velocity[0] - 10 * pow((double)(float).02, .1)) <
		1e-12);
	state.sleep_time = 6;
	CHECK(mh_object_integrate(&state, .1, &next, NULL) == MH_ERROR_NONE &&
		next.flags == 3 && next.sleep_time == 0);

	/* Timer sleep still completes this update. */
	CHECK(next.motion.position[0] > 0);
	state.motion.position[1] = -100.01;
	CHECK(mh_object_integrate(&state, .1, &next, NULL) == MH_ERROR_NONE &&
		next.flags == 3 && next.sleep_time == 0);
	CHECK(memcmp(&next.motion, &state.motion, sizeof(state.motion)) == 0);
	saved = next;
	CHECK(mh_object_integrate(&state, -1, &next, NULL) != MH_ERROR_NONE &&
		memcmp(&saved, &next, sizeof(next)) == 0);
	state.flags = 1;
	CHECK(mh_object_integrate(&state, 0, &next, NULL) == MH_ERROR_NONE &&
		next.flags == 3 && next.sleep_time == 0);
	CHECK(memcmp(&next.motion, &state.motion, sizeof(state.motion)) == 0);
	state.motion.position[1] = 10;
	state.sleep_time = 6.1;
	CHECK(mh_object_integrate(&state, 0, &next, NULL) == MH_ERROR_NONE &&
		next.flags == 3 && next.sleep_time == 0);
	CHECK(memcmp(&next.motion, &state.motion, sizeof(state.motion)) == 0);
	state.sleep_time = 0;
	CHECK(mh_object_integrate(&state, 1, &next, NULL) == MH_ERROR_NONE &&
		next.sleep_time == 1 && next.flags == 1);
	saved = next;
	CHECK(mh_object_integrate(&state, 1.001, &next, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &next, sizeof(next)) == 0);
	state.motion.position[1] = -100;
	state.sleep_time = 0;
	CHECK(mh_object_integrate(&state, .1, &state, NULL) == MH_ERROR_NONE &&
		state.motion.position[1] < -100);

	return 0;
}

static int
check_rotation_drift(void)
{
	/*
	 * Track 8 scenery impact with binary53 intermediates. The second
	 * column's squared length is 1 - 1.0801924947845265e-6. Rejecting or
	 * normalizing this basis changes subsequent contacts.
	 */
	static const double rotation[9] = {.1740259975194931,
		-.010706346482038498, -.98468273878097534, -.98473268747329712,
		.0020901188254356384, -.17405770719051361, .003921708557754755,
		.99994033575057983, -.010179107077419758};

	struct mh_object_state state, next, saved;
	unsigned int i;

	memset(&state, 0, sizeof(state));
	memcpy(state.motion.rotation, rotation, sizeof(rotation));

	for (i = 0; i < 4; i++)
		state.motion.extension[i] = 1;

	CHECK(!mh_rotation_valid(rotation));
	CHECK(!mh_motion_valid(&state.motion));
	CHECK(mh_motion_valid_object(&state.motion));
	CHECK(mh_object_integrate(&state, 0, &next, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(next.motion.rotation, rotation, sizeof(rotation)) == 0);

	/* Drift accumulates over many contacts; do not fit just one sample. */
	for (i = 0; i < 9; i++)
		state.motion.rotation[i] *= .99999;

	CHECK(mh_object_integrate(&state, 0, &next, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(next.motion.rotation, state.motion.rotation,
		sizeof(rotation)) == 0);
	saved = next;

	/* Malformed bases still fail, preserving caller-owned output. */
	for (i = 0; i < 5; i++) {
		memcpy(state.motion.rotation, rotation, sizeof(rotation));

		if (i == 0)
			memset(state.motion.rotation, 0, sizeof(rotation));
		else if (i == 1) {
			state.motion.rotation[0] *= -1;
			state.motion.rotation[1] *= -1;
			state.motion.rotation[2] *= -1;
		} else if (i == 2)
			memcpy(state.motion.rotation + 3, state.motion.rotation,
				3 * sizeof(double));
		else if (i == 3)
			state.motion.rotation[4] += .1;
		else
			state.motion.rotation[0] = HUGE_VAL;

		CHECK(mh_object_integrate(&state, 0, &next, NULL) ==
			MH_ERROR_INVALID);
		CHECK(memcmp(&next, &saved, sizeof(next)) == 0);
	}

	return 0;
}

int
main(void)
{
	unsigned char bytes[278];
	struct mh_collision hull;
	struct mh_object_body object, saved;
	struct mh_scene_object scene;
	struct mh_car car;
	struct mh_motion_state car_motion, object_motion;
	struct mh_pair_result pair, before;
	struct mh_contact constraints[8];
	double low[3] = {-1, -1, -2}, high[3] = {1, 1, 2};
	double position[3] = {0, 0, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int i;

	CHECK(check_motion() == 0);
	CHECK(check_rotation_drift() == 0);
	memset(&scene, 0, sizeof(scene));
	CHECK(!mh_object_has_body(&scene));
	scene.has_shadow_options = 1;
	CHECK(mh_object_has_body(&scene));
	scene.unseen_by_rays = 1;
	CHECK(!mh_object_has_body(&scene));
	CHECK(mh_object_body_bounds(low, high, .05, 3, &object, NULL) ==
		MH_ERROR_NONE);
	CHECK(object.mass == 5 && object.flags == 2 && object.sound_class == 3);
	CHECK(object.inertia[0] == (double)(float)(10.0 / 3));
	CHECK(object.inertia[1] == (double)(float)(10.0 / 3));
	CHECK(object.inertia[2] == (double)(float)(10.0 / 3));
	CHECK(object.corners[0][0] == 1 && object.corners[0][1] == 1 &&
		object.corners[0][2] == 2);
	CHECK(object.corners[7][0] == -1 && object.corners[7][1] == -1 &&
		object.corners[7][2] == -2);
	saved = object;
	low[0] = 2;
	CHECK(mh_object_body_bounds(low, high, .05, 3, &object, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &object, sizeof(object)) == 0);
	low[0] = -1;
	CHECK(mh_object_body_bounds(low, high, 0, 3, &object, NULL) ==
			MH_ERROR_NONE &&
		object.mass > 10000 && object.flags == 3);
	CHECK(mh_object_body_bounds(low, high, 100, 3, &object, NULL) ==
			MH_ERROR_NONE &&
		object.mass == 10000 && object.flags == 3);
	object = saved;
	CHECK(mh_collision_col_decode(bytes, pair_fixture(bytes), &hull,
		NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_corners = car.has_suspension = 1;
	car.mass = 1000;

	for (i = 0; i < 4; i++)
		car.corners[i][0] = i == 0 || i == 3 ? -1 : 1;

	CHECK(mh_motion_reset(position, rotation, &object_motion, NULL) ==
		MH_ERROR_NONE);
	car_motion = object_motion;
	car_motion.position[0] = 1.5;
	car_motion.velocity[0] = -10;
	CHECK(mh_pair_step_object(&car, &hull, &car_motion, &object, &hull,
		&object_motion, NULL, &pair, NULL) == MH_ERROR_NONE);
	CHECK(pair.contact.hit && pair.sound_severity == 20 &&
		pair.impulse > 0);
	CHECK(pair.state[0].position[0] == car_motion.position[0] &&
		fabs(pair.state[1].position[0] + .6) < 1e-12);
	CHECK(fabs(pair.velocity_delta[0][0] * car.mass +
		pair.velocity_delta[1][0] * object.mass) < 1e-9);
	CHECK(fabs(pair.angular_delta[1][1]) > fabs(pair.angular_delta[0][1]));
	memset(constraints, 0, sizeof(constraints));
	constraints[7].hit = 1;
	constraints[7].normal[0] = 1;
	CHECK(mh_pair_step_object(&car, &hull, &car_motion, &object, &hull,
		&object_motion, constraints, &pair,
		NULL) == MH_ERROR_NONE);
	CHECK(pair.contact.hit && pair.correction[0] == 0 &&
		pair.state[1].position[0] == 0);
	before = pair;
	object.inertia[2] = 0;
	CHECK(mh_pair_step_object(&car, &hull, &car_motion, &object, &hull,
		&object_motion, NULL, &pair, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&before, &pair, sizeof(pair)) == 0);

	mh_collision_free(&hull);

	return 0;
}
