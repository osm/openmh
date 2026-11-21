#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../support/collision_fixture.h"
#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/assets/material.h"
#include "engine/assets/route.h"
#include "engine/collision/data.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/body_internal.h"
#include "engine/simulation/collision.h"
#include "engine/simulation/drive.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field.h"
#include "engine/simulation/grip.h"
#include "engine/simulation/motion.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/pair.h"
#include "engine/simulation/suspension.h"
#include "engine/simulation/vehicle.h"
#include "engine/simulation/vehicle_placement.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-10)

static void
test_query_stores(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_contact hit;
	double start[3] = {
		41.50994873046875, 57.364593505859375, 41.90552520751953};

	double end[3] = {
		41.52439880371094, 56.86968231201172, 42.633033752441406};

	double plane[4] = {-0.02999434433877468, 0.6102578639984131,
		-0.7916347980499268, -0.44093525409698486};

	unsigned int i;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	col.depth = 10;
	col.scale = col.cell_size = 1;
	col.leaves[0].count = 1;

	for (i = 0; i < 3; i++) {
		col.translation[i] = 512;
		col.dimensions[i] = 1024;
	}

	memcpy(col.polygons[0].plane, plane, sizeof(plane));
	col.polygons[0].boundary_count = 3;
	memset(col.polygons[0].boundaries, 0,
		sizeof(col.polygons[0].boundaries));
	col.polygons[0].boundaries[0][0] = -1;
	col.polygons[0].boundaries[0][3] = 200;
	col.polygons[0].boundaries[1][1] = 1;
	col.polygons[0].boundaries[1][3] = 500;
	col.polygons[0].boundaries[2][2] = 1;
	col.polygons[0].boundaries[2][3] = 500;

	/* Cancellation and separate point stores. */
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.fraction == 0.16772206127643585);
	CHECK(hit.position[0] == 41.51237106323242);
	CHECK(hit.position[1] == 57.281585693359375);
	CHECK(hit.position[2] == 42.02754592895508);

	/* Rounding the edge distance can reject a double-precision hit. */
	memset(col.polygons[0].plane, 0, sizeof(plane));
	col.polygons[0].plane[1] = 1;
	col.polygons[0].boundaries[0][3] = 0;
	start[0] = end[0] = .001 - 1e-12;
	start[1] = .5;
	end[1] = -.5;
	start[2] = end[2] = 0;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(!hit.hit);
	start[0] = end[0] = 0.0009999999310821295;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.fraction == .5);

	/* Original compares each raw quotient with the stored winner. */
	col.leaves[0].count = 2;
	col.polygons[0].plane[3] = -0.09999867528676987;
	col.polygons[1].plane[3] = -0.09999868273735046;
	start[0] = end[0] = 0;
	start[1] = 1;
	end[1] = -.7;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.polygon == 1 && hit.fraction == 0.529412567615509);
	col.polygon_indices[0] = 1;
	col.polygon_indices[1] = 0;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.polygon == 0 && hit.fraction == 0.529412567615509);
	mh_collision_free(&col);
}

static void
test_body_endpoint_storage(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_motion_state entry, current;
	struct mh_body_result result;
	struct mh_contact raw;
	double position[3] = {0, .5, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	int hit;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = car.has_corners = 1;
	car.mass = 1000;
	CHECK(mh_motion_reset(position, rotation, &entry, NULL) ==
		MH_ERROR_NONE);
	current = entry;
	current.position[1] = (double)(float).08 - 1e-10;
	current.velocity[1] = -1;

	/*
	 * The query stores its endpoint and biased length as binary32. This
	 * rounds the start-distance rejection threshold to .5, so both the
	 * direct query and body callers miss without mutating the input pose.
	 */
	CHECK(mh_collision_cast(&col, entry.position, current.position, &raw,
		NULL) == MH_ERROR_NONE &&
		!raw.hit);
	CHECK(mh_body_begin(&car, &col, &entry, &current, .01, 4, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_body_impact_pass(&car, &col, &entry, .01, &result, &hit,
		NULL) == MH_ERROR_NONE);
	CHECK(!hit && result.impact_count == 0);
	CHECK(current.position[1] == (double)(float).08 - 1e-10);

	/*
	 * Residuals share the endpoint boundary, then store even zero pushes
	 * through the original translation helpers.
	 */
	CHECK(mh_body_begin(&car, &col, &entry, &current, .01, 4, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_body_residual(&car, &col, &entry, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.residual_count == 0 && result.state.velocity[1] == -1);
	CHECK(result.state.position[1] == (float)current.position[1]);
	mh_collision_free(&col);
}

static void
test_body_response_stores(void)
{
	static const double expected[2][18] = {
		{.11123835295438766, .04509960114955902, .10000000149011612,
			.8030464053153992, .5959072709083557,
			-.00331870443187654, -.5959165096282959,
			.8030361533164978, -.004064659588038921,
			.00024287914857268333, .005241781007498503,
			.9999862313270569, 4.4978461265563965,
			-5.796209812164307, 2.331399440765381,
			-.2357276976108551, .06072536110877991,
			-.16062915325164795},
		{.21093332767486572, -.35902222990989685, .10000000149011612,
			.800000011920929, .6000000238418579, 0,
			-.6000000238418579, .800000011920929, 0, 0, 0, 1,
			4.747733116149902, -3.384577751159668,
			2.3399999141693115, .25, -.3400000035762787,
			.44999998807907104}};

	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_motion_state entry, current;
	struct mh_body_result result;
	double position[3] = {0, 1, 0};
	double rotation[9] = {.8, .6, 0, -.6, .8, 0, 0, 0, 1};
	unsigned int i, pass;
	int hit;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = car.has_corners = 1;
	car.mass = 1234.5;
	car.center_mass[1] = .3;

	for (i = 0; i < 4; i++) {
		car.corners[i][0] = i == 0 || i == 3 ? .4 : -.4;
		car.corners[i][2] = i < 2 ? .5 : -.5;
	}

	CHECK(mh_motion_reset(position, rotation, &entry, NULL) ==
		MH_ERROR_NONE);
	current = entry;
	current.position[0] = current.position[2] = .1;
	current.position[1] = -.5;
	current.velocity[0] = 1.23;
	current.velocity[1] = -7.89;
	current.velocity[2] = 2.34;
	current.angular_velocity[0] = .25;
	current.angular_velocity[1] = -.34;
	current.angular_velocity[2] = .45;

	/*
	 * The tilted off-center impact exercises stored moment products,
	 * impulse, velocity increments and rotation with binary53
	 * intermediates. Four residual hits separately exercise sequential
	 * projections and stored push sums.
	 */
	for (pass = 0; pass < 2; pass++) {
		CHECK(mh_body_begin(&car, &col, &entry, &current, 1.0 / 120, 4,
			&result, NULL) == MH_ERROR_NONE);

		if (pass == 0) {
			CHECK(mh_body_impact_pass(&car, &col, &entry, 1.0 / 120,
				&result, &hit, NULL) == MH_ERROR_NONE &&
				hit);
			CHECK(result.impacts[0].corner == 1);
		} else {
			CHECK(mh_body_residual(&car, &col, &entry, 1.0 / 120,
				&result, NULL) == MH_ERROR_NONE);
			CHECK(result.residual_count == 4);
		}

		for (i = 0; i < 3; i++) {
			CHECK(result.state.position[i] == expected[pass][i]);
			CHECK(result.state.velocity[i] ==
				expected[pass][12 + i]);
			CHECK(result.state.angular_velocity[i] ==
				expected[pass][15 + i]);
		}

		for (i = 0; i < 9; i++)
			CHECK(result.state.rotation[i] ==
				expected[pass][3 + i]);
	}

	mh_collision_free(&col);
}

static void
test_body(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_motion_state entry, current;
	struct mh_body_result result, saved;
	struct mh_body_effects effects;
	struct mh_object_body object;
	double p[3], r[9];
	unsigned int i;
	int hit;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = car.has_corners = 1;
	car.mass = 1000;

	/* Raw CAR corner Y must be ignored: all four actual arms are zero. */
	for (i = 0; i < 4; i++)
		car.corners[i][1] = 99;

	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	p[0] = p[2] = 0;
	p[1] = .6;
	CHECK(mh_motion_reset(p, r, &entry, NULL) == MH_ERROR_NONE);
	current = entry;
	current.position[1] = -.3;
	current.velocity[1] = -9;
	current.previous_velocity[1] = -8;

	/* Check the original pose stores before further passes or residuals. */
	CHECK(mh_body_begin(&car, &col, &entry, &current, .01, 4, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_body_impact_pass(&car, &col, &entry, .01, &result, &hit,
		NULL) == MH_ERROR_NONE &&
		hit);
	CHECK(result.state.position[1] == 0.038999974727630615);
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.feedback.contact && !result.feedback.strong_air_damping);
	CHECK(result.impacts[0].corner == 0 &&
		result.impacts[0].contact.material == 1);
	CHECK(result.impacts[0].contact.fraction == 0.6666666865348816);
	NEAR(result.impacts[0].impulse, 9000);
	NEAR(result.impacts[0].velocity_delta[1], 3);
	CHECK(result.impacts[0].correction[1] == 0.3089999854564667);
	NEAR(result.state.previous_velocity[1], -8);
	CHECK(result.state.position[1] > 0 && result.state.velocity[1] > -9);

	/*
	 * A stronger bounce clears the COL 0.08-unit endpoint bias, so the next
	 * pass really misses. The successful corner records must survive it.
	 */
	current.velocity[1] = -100;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.impact_count == 1 && result.residual_count == 0);

	for (i = 0; i < 4; i++) {
		CHECK(result.contacts[i].hit); /* Later miss preserves the car
						hit too. */
		NEAR(result.contacts[i].normal[1], 1);
	}

	current.velocity[1] = -9;

	/*
	 * Object branch has eight corners and omits the car-only sqrt bounce.
	 */
	memset(&object, 0, sizeof(object));
	object.mass = 1000;

	for (i = 0; i < 3; i++)
		object.inertia[i] = 1000;

	CHECK(mh_body_object_step(&object, &col, &entry, &current, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.impact_count == 1 && !result.feedback.strong_air_damping);
	NEAR(result.impacts[0].velocity_delta[1], 9);
	NEAR(result.state.velocity[1], 0);
	CHECK(result.state.position[1] == 0.09899996966123581);

	for (i = 0; i < 8; i++)
		CHECK(result.contacts[i]
				.hit); /* Retained when later passes miss. */

	/* Output/state aliasing and failure atomicity. */
	CHECK(mh_body_step(&car, &col, &entry, &result.state, .01, &result,
		NULL) == MH_ERROR_NONE);
	saved = result;
	CHECK(mh_body_step(&car, &col, &entry, &result.state, 0, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	car.has_corners = 0;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	car.has_corners = 1;
	entry.rotation[0] = entry.rotation[4] = -1;
	current.rotation[0] = current.rotation[4] = -1;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.feedback.strong_air_damping);

	/* A stationary segment has no hit, even with a penetrating pose. */
	entry = current;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(!result.feedback.contact && result.impact_count == 0 &&
		result.residual_count == 0);
	NEAR(result.state.velocity[1], -9);

	/*
	 * A sideways car sliding along the floor supplies a real nonzero scrape
	 * metric; it must use local normal X and post-impact forward speed.
	 */
	memset(r, 0, sizeof(r));
	r[1] = 1;
	r[3] = -1;
	r[8] = 1;
	CHECK(mh_motion_reset(p, r, &entry, NULL) == MH_ERROR_NONE);
	current = entry;
	current.position[1] = -.3;
	current.velocity[0] = -20;
	current.velocity[2] = 40;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
			MH_ERROR_NONE &&
		result.impact_count > 0);
	NEAR(result.impacts[0].normal_x, 1);
	NEAR(result.impacts[0].forward, 40);
	memset(&effects, 0, sizeof(effects));
	CHECK(mh_body_accumulate(&result, &effects, NULL) == MH_ERROR_NONE);
	CHECK(effects.impulse >= 16500 && effects.effect_material[0] == 1 &&
		effects.scrape_speed[0] == 40);

	/* Only the upper points cross the floor during this inverted fall. */
	memset(r, 0, sizeof(r));
	r[0] = r[4] = -1;
	r[8] = 1;
	p[1] = 1.5;
	CHECK(mh_motion_reset(p, r, &entry, NULL) == MH_ERROR_NONE);
	current = entry;
	current.position[1] = 1.1;
	current.velocity[1] = 9;
	CHECK(mh_body_step(&car, &col, &entry, &current, .01, &result, NULL) ==
			MH_ERROR_NONE &&
		!result.feedback.contact);
	CHECK(mh_body_step_points(&car, &col, &entry, &current, .01, 7, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.feedback.contact && result.feedback.strong_air_damping &&
		result.impact_count != 0);
	CHECK(result.impacts[0].corner >= 4 && result.impacts[0].corner < 7);
	memset(&effects, 0, sizeof(effects));
	CHECK(mh_body_accumulate(&result, &effects, NULL) == MH_ERROR_NONE &&
		effects.contact && effects.corner[0] >= 4);
	saved = result;
	CHECK(mh_body_step_points(&car, &col, &entry, &current, .01, 8, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	memset(&result, 0, sizeof(result));
	memset(&effects, 0, sizeof(effects));
	result.residual[6].hit = 1;
	result.residual[6].material = 1;
	CHECK(mh_body_accumulate(&result, &effects, NULL) == MH_ERROR_NONE &&
		effects.contact && effects.material == 1);
	mh_collision_free(&col);
}

static void
test_object_ground_frame(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_object_body body;
	struct mh_object_state current, result, saved;
	struct mh_motion_state entry;
	double p[3], r[9];
	unsigned int i;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	memset(&body, 0, sizeof(body));
	body.mass = 1000;

	for (i = 0; i < 3; i++)
		body.inertia[i] = 1000;

	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	p[0] = p[2] = 0;
	p[1] = .6;
	CHECK(mh_motion_reset(p, r, &entry, NULL) == MH_ERROR_NONE);
	memset(&current, 0, sizeof(current));
	current.motion = entry;
	current.motion.position[1] = -.3;
	current.motion.velocity[1] = -9;
	current.flags = 2;
	current.frame_impact = 1;
	current.sleep_time = 3;
	current.contacts[7].hit = 1;
	CHECK(mh_object_ground(&body, &col, &entry, &current, 0, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&result, &current, sizeof(result)) == 0);

	/*
	 * Sleeping skips the main impulse, but eight residual casts still push
	 * by 8*(1-binary32(2/3))*dt. Residual hits never become next-slice
	 * damping contacts.
	 */
	CHECK(mh_object_ground(&body, &col, &entry, &current, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.motion.position[1] == -0.273333340883255);
	CHECK(result.motion.velocity[1] == 0.026666661724448204);
	CHECK(result.flags == 2 && result.frame_impact &&
		result.sleep_time == 3);

	for (i = 0; i < 8; i++)
		CHECK(!result.contacts[i].hit);

	mh_object_begin_frame(&current);
	CHECK(current.contacts[7].hit && current.flags == 2 &&
		current.sleep_time == 3);
	CHECK(mh_object_ground(&body, &col, &entry, &current, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&result.motion, &current.motion, sizeof(result.motion)) ==
		0);
	CHECK(!result.frame_impact);

	for (i = 0; i < 8; i++)
		CHECK(!result.contacts[i].hit);

	/*
	 * An actual main impact sets the latch; an airborne miss in the next
	 * slice clears contacts while preserving that latch until frame reset.
	 */
	current.flags = 0;
	CHECK(mh_object_ground(&body, &col, &entry, &current, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.frame_impact && result.contacts[0].hit &&
		result.contacts[7].hit);
	CHECK(result.motion.position[1] == 0.09899996966123581);
	NEAR(result.motion.velocity[1], 0);
	result.motion.position[1] = 10;
	entry = result.motion;
	CHECK(mh_object_ground(&body, &col, &entry, &result, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.frame_impact);

	for (i = 0; i < 8; i++)
		CHECK(!result.contacts[i].hit);

	/*
	 * Timer-induced sleep still completes integration and can cross a floor
	 * in that final step. Its earlier frame impact permits residual
	 * response.
	 */
	current.motion.position[1] = .1;
	current.motion.velocity[1] = -20;
	current.sleep_time = 6;
	current.frame_impact = 1;
	entry = current.motion;
	CHECK(mh_object_integrate(&current, .01, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.flags == 2 && result.sleep_time == 0 &&
		result.motion.position[1] < 0);
	saved = result;
	CHECK(mh_object_ground(&body, &col, &entry, &result, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.frame_impact && result.flags == 2);
	CHECK(result.motion.position[1] > saved.motion.position[1] &&
		result.motion.velocity[1] > 0);

	for (i = 0; i < 8; i++)
		CHECK(!result.contacts[i].hit);

	/* Fail on the final residual corner after seven valid queries. */
	current = saved;
	current.motion.position[0] = 999999;
	entry = current.motion;
	body.corners[7][0] = 10000;
	saved = result;
	CHECK(mh_object_ground(&body, &col, &entry, &current, .01, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	saved = current;
	CHECK(mh_object_ground(&body, &col, &entry, &current, .01, &current,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&current, &saved, sizeof(current)) == 0);
	mh_collision_free(&col);
}

static void
test_grip_motion(void)
{
	static const unsigned char defs[] = "Materials 1\nMaterialName road\n";
	static const unsigned char surfaces[] =
		"Materials 2\nMaterial unused\nMaterial road\n";
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_materials materials;
	struct mh_motion_state state;
	struct mh_motion_forces forces;
	struct mh_motion_result raw;
	struct mh_grip_state grip;
	struct mh_grip_control control;
	struct mh_grip_result post;
	struct mh_grip_step_result result;
	struct mh_grip_step_result saved;
	double p[3];
	double r[9];
	unsigned int i;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	CHECK(mh_materials_mat_srf_decode(defs, sizeof(defs) - 1, surfaces,
		sizeof(surfaces) - 1, &materials, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = car.has_grip = 1;
	car.mass = 1000;
	car.spring_length = .5;
	car.spring_strength = 1000;
	car.brake_force = .5;
	car.grip_level = 5;
	memset(&forces, 0, sizeof(forces));
	memset(&grip, 0, sizeof(grip));

	for (i = 0; i < 4; i++)
		grip.wheels[i].material = MH_GRIP_NO_MATERIAL;

	memset(&control, 0, sizeof(control));
	control.gear = 1;
	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	p[0] = p[2] = 0;
	p[1] = .3;
	CHECK(mh_motion_reset(p, r, &state, NULL) == MH_ERROR_NONE);
	state.velocity[2] = 5;
	state.previous_velocity[2] = 4;

	/*
	 * No OLD contacts: no material changes before integration. NEW contacts
	 * must affect damping after it, without overwriting tick-entry history.
	 */
	CHECK(mh_motion_step(&car, &col, &state, &forces, .01, &raw, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_grip_finish(&car, &materials, &grip, &control, &raw.state,
		&raw.passes[2], .01, &post, NULL) == MH_ERROR_NONE);
	CHECK(mh_grip_step(&car, &col, &materials, &state, &forces, &grip,
		&control, .01, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.grip.contacts == 4);
	NEAR(result.grip.state.phase, 0);
	NEAR(result.motion.state.previous_velocity[2], 5);

	for (i = 0; i < 3; i++) {
		NEAR(result.motion.state.position[i], raw.state.position[i]);
		NEAR(result.motion.state.velocity[i], post.velocity[i]);
	}

	CHECK(result.motion.state.velocity[2] < raw.state.velocity[2]);

	/*
	 * Both state inputs may alias the output; retained contacts now advance
	 * phase once per wheel. A later failure must leave the whole result
	 * alone.
	 */
	CHECK(mh_grip_step(&car, &col, &materials, &result.motion.state,
		&forces, &result.grip.state, &control, .01, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.grip.state.phase == .6239999532699585);
	saved = result;
	car.has_grip = 0;
	CHECK(mh_grip_step(&car, &col, &materials, &result.motion.state,
		&forces, &result.grip.state, &control, .01, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	mh_collision_free(&col);
}

static void
test_vehicle(void)
{
	static const unsigned char defs[] = "Materials 1\nMaterialName road\n";
	static const unsigned char surfaces[] =
		"Materials 2\nMaterial unused\nMaterial road\n";
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_materials materials;
	struct mh_engine_options options;
	struct mh_engine_config config;
	struct mh_vehicle_state state;
	struct mh_vehicle_input input;
	struct mh_vehicle_result result;
	struct mh_vehicle_result saved;
	double p[3];
	double r[9];
	unsigned int i;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	CHECK(mh_materials_mat_srf_decode(defs, sizeof(defs) - 1, surfaces,
		sizeof(surfaces) - 1, &materials, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	memset(&options, 0, sizeof(options));
	memset(&input, 0, sizeof(input));
	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	car.has_engine = car.has_grip = car.has_suspension = car.has_steering =
		car.has_corners = 1;
	car.mass = 1000;
	car.spring_length = .5;
	car.spring_strength = 100000;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.brake_force = .5;
	car.acceleration_force = 2800;
	car.acceleration_level = car.speed_level = car.grip_level = 5;
	car.gears = 1;
	car.gear_ratio[0] = 50;
	car.gear_ratio[1] = 70;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	p[0] = p[2] = 0;
	p[1] = .3;
	CHECK(mh_vehicle_reset(&config, p, r, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.speed == 0);
	input.controls.throttle = 1;
	CHECK(mh_vehicle_step(&car, &col, &materials, &config, &state, &input,
		.01, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.engine.state.detached && result.engine.wheelspin);
	CHECK(result.road.grip.contacts == 4 &&
		result.state.motion.velocity[2] > 0);
	CHECK(result.state.body_points == 4);
	CHECK(result.state.speed ==
		0); /* Captured before this tick accelerates. */
	NEAR(result.state.grip.rear_limit, 2000);
	NEAR(result.state.motion.previous_velocity[2], 0);

	for (i = 2; i < 4; i++)
		NEAR(result.forces.forces.traction[i][2], 6000);

	/*
	 * On the second tick, the preceding contact limit inhibits wheelspin;
	 * initial material zero and subsequent contacts both advance phase.
	 */
	state = result.state;
	CHECK(mh_vehicle_step(&car, &col, &materials, &config, &result.state,
		&input, .01, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.state.speed ==
		(float)fabs(state.motion.velocity[2] * 3.6));
	CHECK(!result.engine.wheelspin && !result.engine.state.detached);
	NEAR(result.state.grip.rear_limit, 3000);
	CHECK(result.state.grip.phase == 1.247999906539917);
	saved = result;
	input.body_contact = 2;
	CHECK(mh_vehicle_step(&car, &col, &materials, &config, &result.state,
		&input, .01, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	input.body_contact = 0;
	state.motion.position[1] = 100;

	for (i = 0; i < 3; i++) {
		input.role = i;
		CHECK(mh_vehicle_step(&car, &col, &materials, &config, &state,
			&input, .01, &result, NULL) == MH_ERROR_NONE);
		CHECK(result.road.grip.contacts == 0 &&
			result.state.body_points == (i == 0 ? 7U : 4U));
		CHECK(result.body.point_count == result.state.body_points);
	}

	saved = result;
	input.role = 0;
	car.has_suspension = 0;
	CHECK(mh_vehicle_step(&car, &col, &materials, &config, &result.state,
		&input, .01, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	mh_collision_free(&col);
}

/*
 * Compare the session's finish override with a vehicle given the controls
 * explicitly. This checks the binding/order, including manual human shifts and
 * handbrake, rather than calling the override helper twice.
 */
static void
test_finished_drive(const struct mh_drive *start)
{
	struct mh_drive drive, saved;
	struct mh_track track;
	struct mh_vehicle_input input;
	struct mh_vehicle_result expected;
	struct mh_vehicle_tuning tuning;
	struct mh_engine_input requested;
	unsigned int sign;
	int ai;

	ai = start->ai_path != NULL;
	track = *start->track;
	track.has_ai_goal_wheel = 1;
	track.ai_goal_wheel = .4f;
	memset(&requested, 0, sizeof(requested));
	requested.throttle = 1;
	requested.brake = .7;
	requested.steering = -.8;
	requested.shift_up = requested.shift_down = requested.handbrake = 1;

	for (sign = 0; sign < 2; sign++) {
		drive = *start;
		drive.track = &track;
		drive.config.options.manual = 1;
		drive.race.finished = 1;
		drive.race.now_ms = 12345;
		drive.race_ticks = 1482;
		drive.horn = 1;
		drive.vehicle.motion.position[2] = sign ? .25 : -.25;
		drive.vehicle.motion.velocity[2] = sign ? -3 : 3;
		memset(&input, 0, sizeof(input));
		input.controls = requested;
		input.role = ai ? 2U : 0U;
		input.controls.throttle = sign ? 0 : (double).05f;
		input.controls.brake = sign ? (double).1f : 0;

		if (ai) {
			input.controls.steering = track.ai_goal_wheel;
			input.controls.shift_up = input.controls.shift_down =
				input.controls.handbrake = 0;
			tuning = drive.opponent.vehicle;
		} else {
			tuning.turn_force = drive.car->turn_force;
			tuning.brake_force = drive.car->brake_force;
			tuning.grip_level = drive.car->grip_level;
		}

		tuning.suspension = &drive.suspension;
		CHECK(mh_vehicle_step_tuned(drive.car, drive.col,
			drive.materials, &drive.config, &tuning,
			&drive.vehicle, &input, MH_DRIVE_DT, &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_drive_tick(&drive, &requested, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&drive.vehicle, &expected.state,
			sizeof(drive.vehicle)) == 0);
		CHECK(drive.input_throttle == input.controls.throttle &&
			drive.input_brake == input.controls.brake);
		CHECK(drive.race.finished && drive.race.now_ms == 12345 &&
			drive.race_ticks == 1482);

		if (drive.track->laps != 0)
			CHECK(drive.race.projection_key !=
				MH_ROUTE_CACHE_EMPTY);

		CHECK(drive.horn == 1);
	}

	track.ai_goal_wheel = 1e20f;
	saved = drive;

	if (ai) {
		CHECK(mh_drive_tick(&drive, &requested, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&drive, &saved, sizeof(drive)) == 0);
	} else
		CHECK(mh_drive_tick(&drive, &requested, NULL) == MH_ERROR_NONE);

	drive = *start;
	drive.track = &track;
	CHECK(mh_drive_tick(&drive, &requested, NULL) ==
		MH_ERROR_NONE); /* Unfinished never reads goal wheel. */
}

static void
test_contact_restart(const struct mh_field *base)
{
	struct mh_field field, cleared;
	struct mh_pair_result retained_pair, cleared_pair;
	struct mh_engine_input input;
	unsigned int event, i;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));

	for (event = 0; event < 2; event++) {
		field = *base;
		field.feedback_pair[0] = 7;
		field.feedback_pair[1] = -3;
		field.feedback_pair[2] = 5;
		field.cars[0].feedback_track_hit = 1;
		memset(field.body_contacts, 0, sizeof(field.body_contacts));
		field.body_contacts[1][0].hit = 1;
		field.body_contacts[1][0].normal[0] = 1;

		if (event)
			field.cars[1].vehicle.motion.position[1] = -101;

		status = event ? mh_field_frame_update(&field, .125f, NULL)
			: mh_field_reset(&field, 1, NULL);

		CHECK(status == MH_ERROR_NONE);

		if (status != MH_ERROR_NONE)
			continue;

		if (event)
			CHECK(field.cars[1].recovery_event == MH_RECOVERY_FALL);

		CHECK(field.feedback_pair[0] == 7 &&
			field.feedback_pair[1] == -3 &&
			field.feedback_pair[2] == 5 &&
			field.cars[0].feedback_track_hit);
		CHECK(field.body_contacts[1][0].hit == 1 &&
			field.body_contacts[1][0].normal[0] == 1);

		/*
		 * Supply the same first collision geometry after either
		 * relocation. Slot 0 meets slot 1 before the latter publishes
		 * fresh track casts.
		 */
		for (i = 0; i < 2; i++)
			field.cars[i].vehicle.motion =
				base->cars[i].vehicle.motion;

		field.cars[0].vehicle.motion.velocity[0] = -8;
		cleared = field;
		memset(cleared.body_contacts[1], 0,
			sizeof(cleared.body_contacts[1]));
		status = mh_pair_step(field.cars[0].car, field.hulls[0],
			&field.cars[0].vehicle.motion, field.cars[1].car,
			field.hulls[1], &field.cars[1].vehicle.motion,
			field.body_contacts[1], &retained_pair, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status != MH_ERROR_NONE)
			continue;

		status = mh_pair_step(field.cars[0].car, field.hulls[0],
			&field.cars[0].vehicle.motion, field.cars[1].car,
			field.hulls[1], &field.cars[1].vehicle.motion,
			cleared.body_contacts[1], &cleared_pair, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status != MH_ERROR_NONE)
			continue;

		CHECK(retained_pair.contact.hit && cleared_pair.contact.hit);
		CHECK(retained_pair.correction[0] == 0 &&
			cleared_pair.correction[0] < 0);
		CHECK(mh_field_frame_tick(&field, &input, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_field_frame_tick(&cleared, &input, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(field.contact_count[0] > 0 &&
			cleared.contact_count[0] > 0);
		CHECK(fabs(field.cars[0].vehicle.motion.position[0] -
			cleared.cars[0].vehicle.motion.position[0]) >
			.01);
		CHECK(field.feedback_pair[0] != 7 ||
			field.feedback_pair[1] != -3 ||
			field.feedback_pair[2] != 5);

		/* Slot 1's completed turn now replaces its old constraints. */
		CHECK(!field.body_contacts[1][0].hit);
	}
}

static void
test_wheel_presentation(const struct mh_drive *start)
{
	static const float times[] = {0, .00005f, .0001f};
	struct mh_drive drive, seeded;
	struct mh_field field;
	struct mh_engine_input controls;
	unsigned int i, steps;

	seeded = *start;
	seeded.wheels.angle[0] = 2;
	seeded.wheels.angle[1] = 3;
	seeded.wheels.variant[0] = 0;
	seeded.wheels.variant[1] = 2;
	seeded.wheels.drawn[0] = seeded.wheels.drawn[1] = 1;
	seeded.vehicle.wheel_rate[0] = 10;
	seeded.vehicle.wheel_rate[1] = 20;
	memset(&controls, 0, sizeof(controls));

	/* Bind before the animation-time gate. */
	for (i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
		drive = seeded;
		CHECK(mh_drive_advance(&drive, &controls, times[i],
			MH_FRAME_MENU, &steps, NULL) == MH_ERROR_NONE &&
			steps == 0);
		CHECK(drive.wheels.angle[0] == 2 && drive.wheels.angle[1] == 3);
		CHECK(drive.wheels.variant[0] == 0 &&
			drive.wheels.variant[1] == 2);
		CHECK(drive.wheels.drawn[0] == 0 && drive.wheels.drawn[1] == 2);

		CHECK(mh_field_start(&seeded, NULL, 0, NULL, 0, &field, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_field_frame_update(&field, times[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(field.cars[0].wheels.angle[0] == 2 &&
			field.cars[0].wheels.angle[1] == 3);
		CHECK(field.cars[0].wheels.variant[0] == 0 &&
			field.cars[0].wheels.variant[1] == 2);
		CHECK(field.cars[0].wheels.drawn[0] == 0 &&
			field.cars[0].wheels.drawn[1] == 2);
	}

	/* Suspension skips presentation entirely, unlike a zero-time frame. */
	drive = seeded;
	CHECK(mh_drive_advance(&drive, &controls, 1, MH_FRAME_SUSPEND, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 0);
	CHECK(memcmp(&drive.wheels, &seeded.wheels, sizeof(drive.wheels)) == 0);
	CHECK(mh_field_start(&seeded, NULL, 0, NULL, 0, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_advance(&field, &controls, 1, MH_FRAME_SUSPEND, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 0);
	CHECK(memcmp(&field.cars[0].wheels, &seeded.wheels,
		sizeof(seeded.wheels)) == 0);
}

/*
 * AI recovery measures distance to the selected shared camera. An
 * opponent's dormant camera must neither allow nor suppress relocation.
 */
static void
test_shared_camera_recovery(
	const struct mh_field *start, const struct mh_engine_input *input)
{
	struct mh_field field, field_slow, field_fast, field_saved;
	unsigned int car, axis;
	double position[3], velocity[3];

	field = *start;

	for (car = 0; car < 8; car++)
		field.cars[car].start_ticks = 1440;

	CHECK(mh_motion_display(&field.cars[1].vehicle.motion, position,
		velocity, NULL) == MH_ERROR_NONE);

	for (axis = 0; axis < 3; axis++) {
		field.cars[0].camera.position[axis] = (float)position[axis];
		field.cars[1].camera.position[axis] = (float)position[axis];
	}

	field.cars[0].camera.position[0] += 200;
	field.cars[1].recovery.ai_seconds = 8;
	field.cars[1].recovery.ai_latched = 1;
	field_slow = field_fast = field;
	field_slow.viewed_entry = 1;
	CHECK(mh_field_tick(&field_slow, input, NULL) == MH_ERROR_NONE);
	CHECK(field_slow.cars[1].recovery_event == MH_RECOVERY_NONE &&
		field_slow.cars[1].recovery.ai_latched);
	CHECK(mh_field_tick(&field, input, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[1].recovery_event == MH_RECOVERY_AI &&
		!field.cars[1].recovery.ai_latched);
	CHECK(field.cars[1].recovery.ai_seconds == 0 &&
		field.cars[1].ticks == 1);
	CHECK(field.cars[1].start_ticks == 1441 &&
		field.cars[1].race_ticks == 1);

	/*
	 * Every recovery, including one following another car's fall,
	 * receives the shared pre-frame-update eye.
	 */
	field_fast.cars[0].vehicle.motion.position[1] = -101;
	field_saved = field_fast;
	CHECK(mh_field_frame_update(&field_fast, 1.0f / 60, NULL) ==
		MH_ERROR_NONE);
	CHECK(field_fast.cars[0].recovery_event == MH_RECOVERY_FALL);
	CHECK(field_fast.cars[1].recovery_event == MH_RECOVERY_AI);
	CHECK(memcmp(field_fast.cars[1].recovery_sample.feedback.camera,
		field_saved.cars[0].camera.position,
		sizeof(field_saved.cars[0].camera.position)) == 0);
	field_fast = field_saved;

	/*
	 * Frame-end recovery already succeeded when the final entrant
	 * fails.
	 */
	field_fast.cars[7].recovery.unsupported_seconds = -1;
	field_saved = field_fast;
	CHECK(mh_field_tick(&field_fast, input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field_fast, &field_saved, sizeof(field_fast)) == 0);
	CHECK(mh_field_reset(&field_slow, 1, NULL) == MH_ERROR_NONE);
	CHECK(!field_slow.cars[1].recovery.ai_latched &&
		field_slow.cars[1].recovery.ai_seconds == 0);
}

/*
 * Launch adjustment precedes the held-grid gate. Multiple physics
 * slices share its output; suspend/recovery retain it and Home reuses
 * bases.
 */
static void
test_launch_tuning(
	const struct mh_field *start, const struct mh_engine_input *input)
{
	struct mh_field field, fresh;
	struct mh_race_tuning_engine expected;
	struct mh_field_tuning tuning;
	struct mh_engine_config last_launch[8];
	unsigned int position, frame, car, gear, steps;

	fresh = field = *start;

	for (frame = 0; frame < 48; frame++) {
		CHECK(mh_field_advance(&field, input, .25, 0, &steps, NULL) ==
				MH_ERROR_NONE &&
			steps == 7);

		if (frame == 26) {
			CHECK(!mh_drive_racing(&field.cars[0]));
			CHECK(field.cars[7].frame.active &&
				field.standings.cars[7].score > 64);
			CHECK(field.cars[7].config.acceleration !=
				fresh.cars[7].config.acceleration);

			for (car = 0; car < 8; car++)
				last_launch[car] = field.cars[car].config;
		}
	}

	CHECK(field.tuning.ticks == 336 && field.tuning.initialized);

	for (car = 0; car < 8; car++) {
		CHECK(memcmp(&field.tuning.base[car], &fresh.cars[car].config,
			sizeof(field.tuning.base[car])) == 0);
		CHECK(memcmp(&field.cars[car].config, &last_launch[car],
			sizeof(last_launch[car])) == 0);
		CHECK(field.cars[car].config.options.suppress_role_two_spin);
	}

	CHECK(field.cars[0].config.acceleration ==
		fresh.cars[0].config.acceleration);
	memset(&expected, 0, sizeof(expected));
	expected.gears = fresh.cars[7].config.gears;
	expected.base_acceleration = (float)fresh.cars[7].config.acceleration;

	for (gear = 0; gear < 7; gear++)
		expected.base_ratio[gear] =
			(float)fresh.cars[7].config.ratio[gear];

	position = field.standings.cars[7].position;
	CHECK(mh_field_advance(&field, input, 2 * MH_DRIVE_DT, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(mh_race_tuning_launch(8, position,
		(float)(((long double)mh_drive_clock_ms(&field.cars[0]) -
				field.cars[0].frame.time.go_ms) *
				(double)(1.0 / 7000) * (double).25 +
			(double)-.25),
		&expected, NULL) == MH_ERROR_NONE);
	CHECK(field.tuning.ticks == 337 &&
		field.cars[7].config.acceleration == expected.acceleration);

	for (gear = 1; gear <= expected.gears; gear++)
		CHECK(field.cars[7].config.ratio[gear] == expected.ratio[gear]);

	field.tuning.curve.progress[0] =
		123; /* Deliberately retained previous-pass state. */
	tuning = field.tuning;
	CHECK(mh_field_advance(&field, input, 100, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(memcmp(&field.tuning, &tuning, sizeof(tuning)) == 0);
	CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&field.tuning, &tuning, sizeof(tuning)) == 0);
	CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);
	tuning.ticks = 0;
	CHECK(memcmp(&field.tuning, &tuning, sizeof(tuning)) == 0);

	for (frame = 0; frame < 48; frame++)
		CHECK(mh_field_advance(&field, input, .25, 0, &steps, NULL) ==
				MH_ERROR_NONE &&
			steps == 7);

	position = field.standings.cars[7].position;
	CHECK(mh_field_advance(&field, input, 2 * MH_DRIVE_DT, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(field.tuning.ticks == 337 && field.cars[0].ticks == 674);
	CHECK(mh_race_tuning_launch(8, position,
		(float)(((long double)mh_drive_clock_ms(&field.cars[0]) -
				field.cars[0].frame.time.go_ms) *
				(double)(1.0 / 7000) * (double).25 +
			(double)-.25),
		&expected, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[7].config.acceleration == expected.acceleration);

	for (gear = 1; gear <= expected.gears; gear++)
		CHECK(field.cars[7].config.ratio[gear] == expected.ratio[gear]);

	CHECK(memcmp(&field.tuning.base[7], &fresh.cars[7].config,
		sizeof(field.tuning.base[7])) == 0);
}

static void
test_drive(void)
{
	static const unsigned char defs[] = "Materials 1\nMaterialName road\n";
	static const unsigned char surfaces[] =
		"Materials 2\nMaterial unused\nMaterial road\n";
	static const unsigned char loop[] =
		"LWMO\n1\n9\n5\n"
		"0 .62 0 0 0 0 1 1 1\n0 1 0 0 0\n"
		"0 .62 10 0 0 0 1 1 1\n10 1 0 0 0\n"
		"10 .62 10 0 0 0 1 1 1\n20 1 0 0 0\n"
		"10 .62 0 0 0 0 1 1 1\n30 1 0 0 0\n"
		"0 .62 0 0 0 0 1 1 1\n40 1 0 0 0\n";
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_materials materials;
	struct mh_route route;
	struct mh_track track;
	struct mh_engine_input input;
	struct mh_drive start, direct, slow, fast, saved;
	struct mh_field field, field_slow, field_fast, field_saved;
	struct mh_route race_route;
	struct mh_scene_key race_keys[65];
	struct mh_standings results;
	unsigned char hull_bytes[278];
	struct mh_collision hulls[2];
	struct mh_car contact_car;
	struct mh_object_body object_body;
	struct mh_scene_object object_scene;
	struct mh_field_object_source object_source;
	double object_low[3] = {-1, -1, -2}, object_high[3] = {1, 1, 2};
	const struct mh_car *roster[7];
	unsigned int j;
	struct mh_ai_path ai_path;
	struct mh_ai_point ai_points[128];
	struct mh_ai_section ai_section;
	struct mh_placement placement;
	struct mh_vehicle_state reset;
	unsigned int i, steps, total;
	float models[MH_DRIVE_MESHES * 16], matrix[16], view[16];
	double p[3], velocity[3], clip[4], x;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	CHECK(mh_materials_mat_srf_decode(defs, sizeof(defs) - 1, surfaces,
		sizeof(surfaces) - 1, &materials, NULL) == MH_ERROR_NONE);
	CHECK(mh_route_mot_parse(loop, sizeof(loop) - 1, &route, NULL) ==
		MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	memset(&input, 0, sizeof(input));
	track.has_start_grid = 1;
	car.has_engine = car.has_grip = car.has_suspension = car.has_steering =
		car.has_corners = 1;
	car.mass = 1000;
	car.spring_length = .5;
	car.spring_strength = 100000;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.brake_force = .5;
	car.acceleration_force = 2800;
	car.acceleration_level = car.speed_level = car.grip_level = 5;
	car.gears = 1;
	car.gear_ratio[0] = 50;
	car.gear_ratio[1] = 70;
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0, 0, 0,
		&start, NULL) == MH_ERROR_NONE);
	test_finished_drive(&start);
	test_wheel_presentation(&start);

	/*
	 * In reverse, full throttle is heavy braking and clears the reverse
	 * drive pedal. Returning to first gear on a later slice must not
	 * restore the old brake until a new input frame. Check both frame APIs.
	 */
	input.throttle = 1;
	input.brake = .7;
	direct = start;
	direct.vehicle.engine.gear = 0;
	CHECK(mh_drive_advance(&direct, &input, .08, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 2);
	CHECK(direct.vehicle.engine.gear == 1 && direct.input_throttle == 1 &&
		direct.input_brake == 0);
	CHECK(mh_drive_advance(&direct, &input, MH_DRIVE_DT, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(direct.input_brake == (float).7);
	CHECK(mh_field_start(&start, NULL, 0, NULL, 1, &field, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].vehicle.engine.gear = 0;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.cars[0].input_brake == 0);
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE &&
		field.cars[0].input_brake == 0);
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.cars[0].input_brake == (float).7);
	CHECK(mh_drive_reset(&direct, 1, NULL) == MH_ERROR_NONE &&
		direct.input_brake == 0 && direct.input_throttle == 0);
	memset(&input, 0, sizeof(input));

	/*
	 * An earlier frame hit remains published during later airborne slices.
	 * Its roof flag uses the new slice's orientation, not the impact pose.
	 */
	CHECK(mh_field_start(&start, NULL, 0, NULL, 1, &field, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].vehicle.motion.position[1] = 100;
	memset(field.cars[0].vehicle.motion.rotation, 0,
		sizeof(field.cars[0].vehicle.motion.rotation));
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] = -1;
	field.cars[0].vehicle.motion.rotation[8] = 1;
	CHECK(mh_body_effect_contact(&field.cars[0].body_effects, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].vehicle.body.contact &&
		field.cars[0].vehicle.body.strong_air_damping);
	CHECK(field.cars[0].vehicle.body_points == 7);
	CHECK(field.cars[0].vehicle.body.material == 1);
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] = 1;
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].vehicle.body.contact &&
		!field.cars[0].vehicle.body.strong_air_damping);
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] = -1;
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE &&
		field.cars[0].vehicle.body.strong_air_damping);

	/*
	 * Starting a new frame clears producers, but the preceding publication
	 * must still reach that frame's first road/grip calculation.
	 */
	field.cars[0].vehicle.motion.velocity[1] = 20;
	field_saved = field;
	field_saved.cars[0].vehicle.body.strong_air_damping = 0;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_frame_tick(&field_saved, &input, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(!field.cars[0].vehicle.body.contact &&
		!field.cars[0].vehicle.body.strong_air_damping);
	CHECK(field.cars[0].vehicle.motion.velocity[1] <
		field_saved.cars[0].vehicle.motion.velocity[1]);

	/*
	 * Frame subdivision retains a strong earlier event; standalone ticks
	 * and the first slice of the next frame clear it.
	 */
	CHECK(mh_field_start(&start, NULL, 0, NULL, 1, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_body_effect_impact(&field.cars[0].body_effects, 1, 3, 1000000,
		1, 40, NULL) == MH_ERROR_NONE);
	CHECK(mh_impact_add(&field.cars[0].impact_effects, 2, 100, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE &&
		field.cars[0].body_effects.impulse == 1000000);
	CHECK(field.cars[0].impact_effects.contact &&
		field.cars[0].impact_effects.severity == 100);
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.cars[0].body_effects.impulse < 1000000);
	CHECK(!field.cars[0].impact_effects.contact &&
		field.cars[0].impact_effects.severity == 0);
	field_saved = field;
	CHECK(mh_field_frame_tick(&field, &input, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &field_saved, sizeof(field)) == 0);
	direct = start;
	CHECK(mh_body_effect_impact(&direct.body_effects, 1, 3, 1000000, 1, 40,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE &&
		direct.body_effects.impulse < 1000000);
	CHECK(mh_body_effect_impact(&direct.body_effects, 1, 3, 1000000, 1, 40,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_advance(&direct, &input, 0, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1 && direct.body_effects.impulse < 1000000);
	CHECK(mh_drive_advance(&direct, &input, 1.0 / 60, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1 && direct.body_effects.impulse < 1000000);
	CHECK(mh_body_effect_impact(&direct.body_effects, 1, 3, 1000000, 1, 40,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_impact_add(&direct.impact_effects, 2, 100, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_drive_reset(&direct, 0, NULL) == MH_ERROR_NONE &&
		direct.body_effects.contact && direct.impact_effects.contact);
	direct = slow = fast = start;
	input.throttle = .25;

	for (i = 0; i < 120; i++)
		CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE);

	total = 0;

	for (i = 0; i < 30; i++) {
		CHECK(mh_drive_advance(&slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);
		total += steps;
	}

	CHECK(total == 30 && slow.ticks == 30);
	total = 0;

	for (i = 0; i < 144; i++) {
		CHECK(mh_drive_advance(&fast, &input, 1.0 / 144, 0, &steps,
			NULL) == MH_ERROR_NONE);
		total += steps;
	}

	CHECK(total == 144 && fast.ticks == 144 && direct.ticks == 120);
	CHECK(mh_motion_valid(&slow.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &slow.vehicle, sizeof(direct.vehicle)) !=
		0);
	CHECK(mh_motion_valid(&fast.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &fast.vehicle, sizeof(direct.vehicle)) !=
		0);

	/*
	 * Both original physics and camera smoothing depend on frame cadence.
	 */
	CHECK(memcmp(direct.eye, fast.eye, sizeof(direct.eye)) != 0);
	CHECK(memcmp(direct.eye, slow.eye, sizeof(direct.eye)) != 0);

	/*
	 * Diagnostic slices retain presentation. Actual 60 Hz field frames
	 * agree with the independent one-car session orchestrator.
	 */
	CHECK(mh_field_start(&start, NULL, 0, NULL, 1, &field, NULL) ==
		MH_ERROR_NONE);
	field_slow = field;
	slow = start;

	for (i = 0; i < 30; i++) {
		field_saved = field;
		CHECK(mh_field_frame_tick(&field, &input, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_field_frame_tick(&field, &input, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(memcmp(&field.cars[0].camera, &field_saved.cars[0].camera,
			sizeof(start.camera)) == 0);
		CHECK(memcmp(&field.cars[0].wheels, &field_saved.cars[0].wheels,
			sizeof(start.wheels)) == 0);
		CHECK(mh_field_frame_update(&field, 1.0f / 60, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_field_advance(&field_slow, &input, 1.0 / 60, 0, &steps,
			NULL) == MH_ERROR_NONE &&
			steps == 1);
		CHECK(mh_drive_advance(&slow, &input, 1.0 / 60, 0, &steps,
			NULL) == MH_ERROR_NONE &&
			steps == 1);
		CHECK(memcmp(&slow.vehicle, &field_slow.cars[0].vehicle,
			sizeof(start.vehicle)) == 0);
		CHECK(memcmp(&slow.camera, &field_slow.cars[0].camera,
			sizeof(start.camera)) == 0);
		CHECK(memcmp(&slow.wheels, &field_slow.cars[0].wheels,
			sizeof(start.wheels)) == 0);
	}

	field_saved = field;
	steps = 123;
	CHECK(mh_field_advance(&field, &input, 1, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(memcmp(&field.cars[0].camera, &field_saved.cars[0].camera,
		sizeof(start.camera)) == 0);
	CHECK(memcmp(&field.cars[0].wheels, &field_saved.cars[0].wheels,
		sizeof(start.wheels)) == 0);
	field_saved = field;
	CHECK(mh_field_camera_update(&field, -1, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &field_saved, sizeof(field)) == 0);

	/*
	 * Every positive frame runs physics, including frames shorter than
	 * 1/120.
	 */
	field = field_saved;
	CHECK(mh_field_advance(&field, &input, MH_DRIVE_DT * .1, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(memcmp(&field.cars[0].vehicle, &field_saved.cars[0].vehicle,
		sizeof(start.vehicle)) != 0);
	CHECK(memcmp(&field.cars[0].camera, &field_saved.cars[0].camera,
		sizeof(start.camera)) != 0);
	NEAR(direct.seconds, 1);
	NEAR(fast.accumulator, 0);
	NEAR(slow.accumulator, 0);
	slow = start;
	CHECK(mh_drive_advance(&slow, &input, MH_DRIVE_DT * .5, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(slow.accumulator == 0);
	CHECK(mh_drive_advance(&slow, &input, 10, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(slow.accumulator == 0 && slow.seconds == MH_DRIVE_DT * .5);
	CHECK(mh_drive_advance(&slow, &input, 2, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 10);
	CHECK(slow.ticks == 11);
	NEAR(slow.seconds, 2 + MH_DRIVE_DT * .5);
	saved = slow;
	steps = 123;
	CHECK(mh_drive_advance(&slow, &input, -1, 0, &steps, NULL) !=
			MH_ERROR_NONE &&
		steps == 123);
	CHECK(memcmp(&slow, &saved, sizeof(slow)) == 0);
	input.shift_up = 2;
	CHECK(mh_drive_advance(&slow, &input, .25, 0, &steps, NULL) !=
			MH_ERROR_NONE &&
		steps == 123);
	CHECK(memcmp(&slow, &saved, sizeof(slow)) == 0);
	CHECK(mh_drive_reset(&slow, 2, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&slow, &saved, sizeof(slow)) == 0);
	CHECK(mh_drive_reset(&slow, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&slow.vehicle.motion, &start.vehicle.motion,
		sizeof(slow.vehicle.motion)) == 0);
	CHECK(memcmp(slow.vehicle.grip.wheels, saved.vehicle.grip.wheels,
		sizeof(slow.vehicle.grip.wheels)) == 0);
	CHECK(slow.vehicle.engine.detached == saved.vehicle.engine.detached);
	CHECK(slow.vehicle.smoothed_steering ==
		saved.vehicle.smoothed_steering);
	CHECK(memcmp(slow.eye, saved.eye, sizeof(slow.eye)) == 0);
	CHECK(slow.ticks == saved.ticks && slow.seconds == saved.seconds &&
		slow.wheels.angle[0] == 0 && slow.wheels.angle[1] == 0);
	CHECK(mh_motion_display(&direct.vehicle.motion, p, velocity, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_placement_recover(&route, p, &placement, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_vehicle_relocate(&direct.config, &placement, &direct.vehicle,
		&reset, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_reset(&direct, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&direct.vehicle, &reset, sizeof(reset)) == 0);
	CHECK(direct.ticks == 120 && direct.accumulator == 0);

	/* Countdown samples the absolute clock at different frame cadences. */
	input.shift_up = 0;
	CHECK(mh_drive_start_phase(&start) == MH_START_RACING);
	saved = start;
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0, 0, 2,
		&start, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &start, sizeof(start)) == 0);
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0, 0, 1,
		&direct, NULL) == MH_ERROR_NONE);
	slow = fast = direct;
	CHECK(mh_drive_start_phase(&direct) == MH_START_READY);

	for (i = 0; i < 1680; i++) {
		CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE);
		CHECK(direct.start_ticks == i + 1);
		CHECK((unsigned int)mh_drive_start_phase(&direct) ==
			(i + 1 < 720 ? 0 : (i + 1 - 720) / 240 + 1));
	}

	for (i = 0; i < 420; i++)
		CHECK(mh_drive_advance(&slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2016; i++)
		CHECK(mh_drive_advance(&fast, &input, 1.0 / 144, 0, &steps,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_motion_valid(&slow.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &slow.vehicle, sizeof(direct.vehicle)) !=
		0);
	CHECK(mh_motion_valid(&fast.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &fast.vehicle, sizeof(direct.vehicle)) !=
		0);
	CHECK(mh_drive_racing(&fast) && mh_drive_racing(&slow));
	NEAR(fast.seconds, 14);
	NEAR(slow.seconds, 14);

	/*
	 * An interactive window after --ticks must retain the released clock.
	 */
	saved = direct;
	saved.race.now_ms =
		2000; /* Published two-second diagnostic race clock. */
	CHECK(mh_drive_advance(&saved, &input, 0, 0, &steps, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_drive_racing(&saved) && saved.frame.time.race_ms >= 2000);
	CHECK(saved.frame.time.race_ms <= 2001);
	CHECK(mh_drive_advance(&saved, &input, 1.0 / 60, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(saved.frame.time.race_ms >= 2016 &&
		saved.frame.time.race_ms <= 2018);
	CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE &&
		direct.start_ticks == 1680);
	CHECK(mh_drive_reset(&direct, 1, NULL) == MH_ERROR_NONE &&
		direct.start_ticks == 0);
	CHECK(mh_drive_advance(&direct, &input, .25, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		direct.start_ticks >= 29 && direct.start_ticks <= 30);
	saved = direct;
	CHECK(mh_drive_advance(&direct, &input, 10, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		direct.start_ticks >= 29 && direct.start_ticks <= 30);
	CHECK(memcmp(&direct.vehicle, &saved.vehicle, sizeof(direct.vehicle)) ==
		0);
	CHECK(mh_drive_reset(&direct, 0, NULL) == MH_ERROR_NONE &&
		direct.start_ticks >= 29 && direct.start_ticks <= 30);

	/* World units, display-origin offset, wheel suspension and steering. */
	slow = start;
	memset(slow.vehicle.motion.rotation, 0,
		sizeof(slow.vehicle.motion.rotation));
	slow.vehicle.motion.rotation[0] = slow.vehicle.motion.rotation[4] =
		slow.vehicle.motion.rotation[8] = 1;
	car.wheel_position[0][0] = -.8;
	car.wheel_position[0][1] = .2;
	car.wheel_position[0][2] = 1.5;
	slow.vehicle.motion.extension[0] = .4;
	slow.vehicle.smoothed_steering = .5;
	CHECK(mh_motion_display(&slow.vehicle.motion, p, velocity, NULL) ==
		MH_ERROR_NONE);
	mh_drive_matrices(&slow, 1.5, models, matrix, view);

	for (i = 0; i < 16; i++)
		CHECK(models[i] == (i % 5 == 0 ? 1 : 0));

	for (i = 0; i < 3; i++)
		CHECK(fabs(models[28 + i] - p[i]) < 1e-6);

	CHECK(fabs(models[44] - p[0] + 1) < 1e-6);
	CHECK(fabs(models[45] - p[1]) < 1e-6);
	CHECK(fabs(models[46] - p[2] - 1.5) < 1e-6);
	CHECK(fabs(models[32] - cos(.35)) < 1e-6 &&
		fabs(models[40] - sin(.35)) < 1e-6);
	CHECK(models[64] == 1 &&
		models[74] == 1); /* Rear wheels do not steer. */

	for (i = 0; i < 4; i++) {
		clip[i] = matrix[i] * slow.target[0] +
			matrix[4 + i] * slow.target[1] +
			matrix[8 + i] * slow.target[2] + matrix[12 + i];
	}

	CHECK(clip[3] > 0 && fabs(clip[0] / clip[3]) < 1e-6 &&
		fabs(clip[1] / clip[3]) < 1e-6);
	CHECK(clip[2] / clip[3] > 0 && clip[2] / clip[3] < 1);

	/* The camera's horizontal right direction must project to the right. */
	x = matrix[0] * slow.forward[2] - matrix[8] * slow.forward[0];
	CHECK(x > 0);

	/* Original viewport has focal 1 horizontally and 4/3 vertically. */
	CHECK(fabs(sqrt(matrix[0] * matrix[0] + matrix[4] * matrix[4] +
			matrix[8] * matrix[8]) -
		1) < 1e-6);
	CHECK(fabs(sqrt(matrix[1] * matrix[1] + matrix[5] * matrix[5] +
			matrix[9] * matrix[9]) -
		4.0 / 3) < 1e-6);
	CHECK(mh_drive_camera_far(&slow) == 300);
	track.view_distance = 150;
	CHECK(mh_drive_camera_far(&slow) == 150);
	track.view_distance = 1;
	CHECK(mh_drive_camera_far(&slow) == 60);
	track.view_distance = 1000;
	CHECK(mh_drive_camera_far(&slow) == 300);
	track.view_distance = 0;

	/*
	 * Inversion advances before view application. Falling back clears the
	 * look-back request; cockpit returns after cumulative supported time.
	 */
	slow.camera_mode = MH_CAMERA_IN_CAR;
	slow.look_back = 0;
	CHECK(mh_drive_camera_update(&slow, 1.0f / 60, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_in_car(&slow));
	CHECK(slow.camera.rotation[4] == 1 &&
		fabs(slow.eye[1] - p[1] - 1) < 1e-5);
	saved = slow;
	slow.look_back = 1;
	CHECK(mh_drive_camera_update(&slow, 1.0f / 60, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_look_back(&slow));
	CHECK(slow.camera.rotation[8] == -1 &&
		fabs(slow.eye[2] - p[2] + 1) < 1e-5);
	slow.vehicle.motion.rotation[4] = slow.vehicle.motion.rotation[8] = -1;
	CHECK(mh_drive_camera_roll_frame(&slow, .25f, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_camera_update(&slow, .25f, NULL) == MH_ERROR_NONE &&
		slow.camera_roll.external);
	CHECK(!mh_drive_camera_in_car(&slow) &&
		!mh_drive_camera_look_back(&slow));
	slow.vehicle.motion.rotation[4] = slow.vehicle.motion.rotation[8] = 1;

	for (i = 0; i < 4; i++)
		slow.vehicle.grip.wheels[i].supported = i != 3;

	CHECK(mh_drive_camera_roll_frame(&slow, .25f, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_camera_update(&slow, .25f, NULL) == MH_ERROR_NONE &&
		!mh_drive_camera_in_car(&slow));
	CHECK(mh_drive_camera_roll_frame(&slow, .25f, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_camera_update(&slow, .25f, NULL) == MH_ERROR_NONE &&
		!mh_drive_camera_in_car(&slow));
	CHECK(mh_drive_camera_roll_frame(&slow, .01f, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_camera_update(&slow, .01f, NULL) == MH_ERROR_NONE &&
		mh_drive_camera_in_car(&slow));
	CHECK(!slow.look_back && !mh_drive_camera_look_back(&slow));
	slow.look_back = 1;
	slow.camera_mode = MH_CAMERA_CHASE;
	CHECK(mh_drive_camera_update(&slow, 0, NULL) == MH_ERROR_NONE &&
		slow.camera_roll.supported_seconds == 0);
	CHECK(!mh_drive_camera_in_car(&slow) &&
		mh_drive_camera_look_back(&slow));
	slow = saved;

	/*
	 * Autonomous mode owns controls at the same fixed tick, and holds the
	 * driver timer during countdown. Assets remain borrowed across copies.
	 */
	memset(ai_points, 0, sizeof(ai_points));
	memset(&ai_section, 0, sizeof(ai_section));
	ai_path.points = ai_points;
	ai_path.count = 128;
	ai_path.sections = &ai_section;
	ai_path.section_count = 1;
	ai_section.count = 128;
	CHECK(mh_placement_grid(&track, &route, 0, &placement, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 128; i++) {
		ai_points[i].x =
			placement.position[0] + i * placement.rotation[6];
		ai_points[i].z =
			placement.position[2] + i * placement.rotation[8];
		memcpy(ai_points[i].direction, placement.rotation + 6,
			3 * sizeof(double));
		ai_points[i].width[0] = ai_points[i].width[1] = 20;
	}

	track.has_ai[1] = 1;
	track.ai[1][8] = 2;
	track.ai[1][9] = 14;
	CHECK(mh_drive_enable_ai(&start, &ai_path, 2, NULL) == MH_ERROR_NONE);
	test_finished_drive(&start);
	direct = slow = fast = start;
	memset(&input, 0, sizeof(input));

	for (i = 0; i < 120; i++)
		CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE);

	input.brake = 1; /* Ignored when AI owns the controls. */

	for (i = 0; i < 30; i++)
		CHECK(mh_drive_advance(&slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < 144; i++)
		CHECK(mh_drive_advance(&fast, &input, 1.0 / 144, 0, &steps,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_motion_valid(&slow.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &slow.vehicle, sizeof(direct.vehicle)) !=
		0);
	CHECK(direct.ticks == 120 && slow.ticks == 30 && fast.ticks == 144);
	CHECK(mh_motion_valid(&fast.vehicle.motion));
	CHECK(memcmp(&direct.vehicle, &fast.vehicle, sizeof(direct.vehicle)) !=
		0);
	saved = direct;
	CHECK(mh_drive_reset(&direct, 1, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&direct.ai, &start.ai, sizeof(direct.ai)) == 0);
	CHECK(direct.vehicle.wheelspin == saved.vehicle.wheelspin);
	direct.countdown = 1;

	for (i = 0; i < 1440; i++)
		CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE);

	/*
	 * Falling below the fixture's tiny collision world can now
	 * relocate even during countdown. AI controls/timers stay held at the
	 * new location.
	 */
	CHECK(direct.ai.throttle == 0 && direct.ai.brake == 0 &&
		direct.ai.steering == 0);
	CHECK(direct.ai.state.stuck_seconds == 0 && !direct.ai.state.reversing);
	CHECK(mh_drive_tick(&direct, &input, NULL) == MH_ERROR_NONE &&
		direct.ai.throttle == 1);
	saved = direct;
	CHECK(mh_drive_reset(&direct, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&direct.ai, &saved.ai, sizeof(direct.ai)) == 0);
	CHECK(direct.vehicle.wheelspin == saved.vehicle.wheelspin);

	/*
	 * Even a failure after AI succeeds must leave both state machines
	 * intact.
	 */
	direct.config.min_rpm = 0;
	saved = direct;
	CHECK(mh_drive_tick(&direct, &input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&direct, &saved, sizeof(direct)) == 0);
	direct.config.min_rpm = start.config.min_rpm;
	saved = direct;
	CHECK(mh_drive_enable_ai(&direct, &ai_path, 4, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&direct, &saved, sizeof(direct)) == 0);

	/*
	 * Eight cars share the sampled frame clock. Cadence changes physical
	 * trajectories; a late failure still rolls back every earlier car.
	 */
	for (i = 0; i < 7; i++)
		roster[i] = &car;

	CHECK(mh_field_start(&start, roster, 7, &ai_path, 2, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.count == 8 && field.standings.count == 0);
	CHECK(field.ai_roster.active_mask == 255);

	for (i = 0; i < 8; i++) {
		CHECK(field.ai_roster.order[i] == i &&
			field.ai_roster.cars[i].speed == 0);
		CHECK(field.ai_roster.cars[i].heading_dot == 1 &&
			field.ai_roster.cars[i].lateral_rate == 0);
	}

	field_saved = field;
	CHECK(mh_field_select_view(&field, 1, NULL) == MH_ERROR_NONE);
	slow = field.cars[1];
	CHECK(mh_drive_camera_update(&slow, 1.0f / 60, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_camera_update(&field, 1.0f / 60, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&field.cars[1].camera, &slow.camera,
		sizeof(slow.camera)) == 0);
	CHECK(memcmp(&field.cars[0], &field_saved.cars[0],
		sizeof(field.cars[0])) == 0);

	for (j = 0; j < 8; j++)
		CHECK(memcmp(&field.cars[j].vehicle,
			&field_saved.cars[j].vehicle,
			sizeof(slow.vehicle)) == 0);

	field = field_saved;

	for (i = 0; i < 8; i++)
		CHECK(field.cars[i].slot == i);

	field_slow = field_fast = field;

	for (i = 0; i < 120; i++)
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 30; i++)
		CHECK(mh_field_advance(&field_slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < 144; i++)
		CHECK(mh_field_advance(&field_fast, &input, 1.0 / 144, 0,
			&steps, NULL) == MH_ERROR_NONE);

	CHECK(field_slow.ai_roster.active_mask == 255 &&
		field_fast.ai_roster.active_mask == 255);
	field_saved = field_slow;
	CHECK(mh_field_reset(&field_saved, 1, NULL) == MH_ERROR_NONE);
	CHECK(field_saved.cars[0].frame.active);

	for (j = 0; j < 8; j++) {
		CHECK(field_slow.cars[j].ticks == 30 &&
			field_fast.cars[j].ticks == 144);
		CHECK(mh_motion_valid(&field_slow.cars[j].vehicle.motion));
		CHECK(mh_motion_valid(&field_fast.cars[j].vehicle.motion));
		CHECK(field_slow.cars[j].frame.time.now_ms ==
			field_slow.cars[0].frame.time.now_ms);
		CHECK(field_fast.cars[j].frame.time.now_ms ==
			field_fast.cars[0].frame.time.now_ms);
	}

	field.cars[7].config.min_rpm = 0;
	field_saved = field;
	steps = 999;
	CHECK(mh_field_tick(&field, &input, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &field_saved, sizeof(field)) == 0);
	CHECK(mh_field_advance(&field, &input, .25, 0, &steps, NULL) !=
			MH_ERROR_NONE &&
		steps == 999);
	CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
	field = field_slow;

	for (j = 0; j < 8; j++)
		field.cars[j].race.split_hold_ms = j * 1000;

	for (j = 0; j < 8; j++)
		field.body_contacts[j][0].material = 10 + j;

	field_saved = field;
	CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&field.cars[1], &field_saved.cars[1],
		7 * sizeof(field.cars[1])) == 0);
	CHECK(memcmp(&field.ai_roster.cars[1], &field_saved.ai_roster.cars[1],
		7 * sizeof(field.ai_roster.cars[1])) == 0);
	CHECK(memcmp(field.ai_roster.order, field_saved.ai_roster.order,
		sizeof(field.ai_roster.order)) == 0);
	CHECK(memcmp(field.body_contacts, field_saved.body_contacts,
		sizeof(field.body_contacts)) == 0);
	CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);

	for (j = 0; j < 8; j++) {
		CHECK(field.cars[j].start_ticks == 0 &&
			field.lateral_rate[j] == 0);
		CHECK(field.ai_roster.order[j] == j &&
			field.ai_roster.cars[j].lateral_rate == 0);
		CHECK(memcmp(field.body_contacts[j],
			field_saved.body_contacts[j],
			sizeof(field.body_contacts[j])) == 0);
		CHECK(field.cars[j].race.split_hold_ms == j * 1000 &&
			field.cars[j].race.split_notice == 0);
	}

	CHECK(mh_field_advance(&field, &input, .003, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(mh_field_advance(&field, &input, 100, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0 && field.accumulator == 0);
	{
		unsigned long random, expected;

		field = field_slow;

		for (i = 1; i < 8; i++)
			CHECK(mh_drive_ai_profile(&field.cars[i], .25f, .75f,
				NULL) == MH_ERROR_NONE);

		field.accumulator = 0;
		random = expected = 123;
		CHECK(mh_field_advance_random(&field, &input, MH_DRIVE_DT, 0,
			&steps, &random, NULL) == MH_ERROR_NONE &&
			steps == 1);

		for (i = 0; i < 21; i++)
			expected = (expected * 1103515245UL + 12345UL) &
				MH_U32_MAX;

		CHECK(random == expected && field.ai_random == random);
		field.cars[7].config.min_rpm = 0;
		field_saved = field;
		steps = 999;
		CHECK(mh_field_advance_random(&field, &input, MH_DRIVE_DT, 0,
			&steps, &random, NULL) != MH_ERROR_NONE);
		CHECK(random == expected && steps == 999 &&
			memcmp(&field, &field_saved, sizeof(field)) == 0);
		field = field_slow;
		CHECK(mh_drive_ai_profile(&field.cars[1], .25f, .75f, NULL) ==
			MH_ERROR_NONE);
		field.cars[1].ai_personality.walk[0] = .9f;
		CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);
		CHECK(field.cars[1].ai_personality.walk[0] == .125f);
		CHECK(field.cars[1].ai_personality.walk[1] == .375f &&
			field.cars[1].ai_personality.walk[2] == 0);
		field.cars[0].countdown = 1;
		CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);
		CHECK(mh_field_advance_random(&field, &input, MH_DRIVE_DT, 0,
			&steps, &random, NULL) == MH_ERROR_NONE);
		CHECK(random == expected);
	}

	/*
	 * Shared contact passes act on both cars at each frame cadence, and
	 * roll back completed road integration if a later hull query fails.
	 */
	field = field_slow;
	field.count = 2;
	field.active_mask = 3;
	contact_car = car;

	for (i = 0; i < 4; i++) {
		contact_car.corners[i][0] = i == 0 || i == 3 ? -1 : 1;
		contact_car.corners[i][2] = i < 2 ? 2 : -2;
	}

	CHECK(mh_collision_col_decode(hull_bytes, pair_fixture(hull_bytes),
		&hulls[0], NULL) == MH_ERROR_NONE);
	hulls[1] = hulls[0];
	field_saved = field;
	CHECK(mh_field_set_hulls(&field, hulls, 1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
	CHECK(mh_field_set_hulls(&field, hulls, 2, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2; i++) {
		field.cars[i].frame.active = 0;
		field.cars[i].countdown = 1;
	}

	CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2; i++) {
		field.cars[i].car = &contact_car;
		field.cars[i].vehicle = start.vehicle;
		field.cars[i].vehicle.motion.position[0] = i == 0 ? 1.5 : 0;
		field.cars[i].vehicle.motion.position[1] = 10;
		field.cars[i].vehicle.motion.position[2] = 20;
	}

	test_contact_restart(&field);
	field_slow = field_fast = field;
	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
	CHECK(field.contact_count[0] > 0 &&
		field.contact_count[0] == field.contact_count[1]);
	CHECK(field.object_contact_count[0] == 0 &&
		field.object_contact_count[1] == 0);
	CHECK(field.cars[0].impact_effects.contact &&
		field.cars[1].impact_effects.contact);
	CHECK(field.cars[0].impact_effects.sound_class == 0 &&
		field.cars[1].impact_effects.sound_class == 0);
	CHECK(field.cars[0].impact_effects.severity ==
		field.cars[1].impact_effects.severity);
	CHECK(field.cars[0].vehicle.motion.position[0] -
			field.cars[1].vehicle.motion.position[0] >
		2);

	/*
	 * Exercise the interleaved contact path as well as the solo path above.
	 */
	field_saved = field;
	field.cars[0].ai_path =
		NULL; /* Local owner alongside the role-2 peer. */
	field.body_contacts[0][6].material = 8;
	field.body_contacts[1][6].material = 9;
	CHECK(mh_body_effect_contact(&field.cars[0].body_effects, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].vehicle.body_points == 7 &&
		field.cars[1].vehicle.body_points == 4);
	CHECK(field.body_contacts[0][6].material == 0 &&
		field.body_contacts[1][6].material == 9);
	CHECK(field.cars[0].vehicle.body.contact &&
		field.cars[0].vehicle.body.material == 1);
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		!field.cars[0].vehicle.body.contact);
	field = field_saved;

	for (i = 1; i < 120; i++)
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 30; i++)
		CHECK(mh_field_advance(&field_slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < 144; i++)
		CHECK(mh_field_advance(&field_fast, &input, 1.0 / 144, 0,
			&steps, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 2; i++) {
		CHECK(mh_motion_valid(&field_slow.cars[i].vehicle.motion));
		CHECK(mh_motion_valid(&field_fast.cars[i].vehicle.motion));
		CHECK(field_slow.cars[i].frame.time.elapsed > 0 &&
			field_fast.cars[i].frame.time.elapsed > 0);
	}

	field_saved = field;
	hulls[1].polygon_count = 0;
	CHECK(mh_field_tick(&field, &input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);

	/*
	 * Human car is seen by traffic while retaining player vehicle tuning.
	 */
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 3, 0, 1,
		&direct, NULL) == MH_ERROR_NONE);

	/*
	 * Single-car scenery contacts, class selection, cadence, and two reset
	 * roles.
	 */
	CHECK(mh_field_start(&start, NULL, 0, NULL, 1, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_set_hulls(&field, hulls, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_object_body_bounds(object_low, object_high, .05, 3,
		&object_body, NULL) == MH_ERROR_NONE);
	memset(&object_scene, 0, sizeof(object_scene));
	object_scene.has_shadow_options = 1;
	object_scene.position[1] = 10;
	object_scene.position[2] = 20;
	object_scene.rotation[0] = object_scene.rotation[4] =
		object_scene.rotation[8] = 1;
	object_source.body = &object_body;
	object_source.hull = &hulls[0];
	object_source.scene = &object_scene;
	object_source.scene_index = 17;
	CHECK(mh_field_set_objects(&field, &object_source, 1, NULL) ==
			MH_ERROR_NONE &&
		field.object_count == 1);
	field.cars[0].car = &contact_car;
	field.cars[0].vehicle.motion.position[0] = 1.5;
	field.cars[0].vehicle.motion.position[1] = 10;
	field.cars[0].vehicle.motion.position[2] = 20;
	field_saved = field;
	{
		struct mh_object_state expected;
		struct mh_motion_state object_entry;
		struct mh_scene_object animated, sampled;
		struct mh_scene_key keys[2];
		double duration;

		duration = 4 * MH_DRIVE_DT;
		field.cars[0].vehicle.motion.position[0] = 100;
		field.objects[0].state.flags = 0;
		field.objects[0].state.motion.velocity[0] = 10;
		field_slow = field_fast = field;
		object_entry = field.objects[0].state.motion;
		CHECK(mh_object_integrate(&field.objects[0].state,
			(double)(float)duration, &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_object_ground(&object_body, &col, &object_entry,
			&expected, (double)(float)duration, &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_field_frame_tick_duration(&field, &input, 1, duration,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&field.objects[0].state, &expected,
			sizeof(expected)) == 0);
		NEAR(field.scene_seconds, duration);

		for (i = 0; i < 3; i++)
			CHECK(mh_field_frame_tick_duration(&field, &input, 0, 0,
				NULL) == MH_ERROR_NONE);

		CHECK(memcmp(&field.objects[0].state, &expected,
			sizeof(expected)) == 0);
		NEAR(field.scene_seconds, duration);
		CHECK(mh_field_advance(&field_slow, &input, duration, 0, &steps,
			NULL) == MH_ERROR_NONE &&
			steps == 1);
		CHECK(memcmp(field.objects, field_slow.objects,
			sizeof(field.objects)) == 0);

		/*
		 * Underwater halves the single physical interval, not
		 * animation.
		 */
		field_fast.cars[0].cheats = MH_CHEAT_UNDERWATER;
		field_fast.cars[0].countdown = 0;
		CHECK(mh_object_integrate(&field_fast.objects[0].state,
			(double)(float)((float)duration * .5), &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_object_ground(&object_body, &col, &object_entry,
			&expected, (double)(float)((float)duration * .5),
			&expected, NULL) == MH_ERROR_NONE);
		CHECK(mh_field_frame_tick_duration(&field_fast, &input, 1,
			duration, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&field_fast.objects[0].state, &expected,
			sizeof(expected)) == 0);
		NEAR(field_fast.scene_seconds, duration);

		/*
		 * A zero interval still sleeps a previously contacting object
		 * whose timer exceeds six, without clearing its retained
		 * contact records.
		 */
		field.objects[0].state.sleep_time = 6.1;
		field.objects[0].state.contacts[7].hit = 1;
		field.objects[0].state.contacts[7].normal[1] = 1;
		CHECK(mh_field_frame_tick_duration(
			&field, &input, 0, 0, NULL) == MH_ERROR_NONE);
		CHECK((field.objects[0].state.flags & 2) &&
			field.objects[0].state.sleep_time == 0);
		CHECK(field.objects[0].state.contacts[7].hit &&
			field.object_contact_count[0] == 0);
		field_fast = field;
		CHECK(mh_field_frame_tick_duration(
			&field, &input, 1, .26, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&field, &field_fast, sizeof(field)) == 0);
		CHECK(mh_field_frame_tick_duration(&field, &input, 0, duration,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&field, &field_fast, sizeof(field)) == 0);

		/*
		 * Animated scenery samples once before the frame's physical
		 * motion. Re-sampling on a later zero slice would erase that
		 * displacement.
		 */
		field = field_saved;
		field.cars[0].vehicle.motion.position[0] = 100;
		animated = object_scene;
		memset(keys, 0, sizeof(keys));
		keys[0].value[1] = keys[1].value[1] = 10;
		keys[0].value[2] = keys[1].value[2] = 20;
		keys[1].value[0] = 3;
		keys[1].time = 1;
		keys[1].linear = 1;
		animated.keys = keys;
		animated.key_count = 2;
		field.objects[0].source.scene = &animated;
		field.objects[0].state.flags = 0;
		field.objects[0].state.motion.velocity[0] = 10;
		expected = field.objects[0].state;
		CHECK(mh_scene_sample(&animated, duration, &sampled, NULL) ==
			MH_ERROR_NONE);
		memcpy(expected.motion.position, sampled.position,
			sizeof(sampled.position));
		memcpy(expected.motion.rotation, sampled.rotation,
			sizeof(sampled.rotation));
		CHECK(mh_object_integrate(&expected, (double)(float)duration,
			&expected, NULL) == MH_ERROR_NONE);
		CHECK(mh_field_frame_tick_duration(&field, &input, 1, duration,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&field.objects[0].state, &expected,
			sizeof(expected)) == 0);
		CHECK(mh_field_frame_tick_duration(
			&field, &input, 0, 0, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&field.objects[0].state, &expected,
			sizeof(expected)) == 0);
	}

	field = field_saved;
	field.cars[0].vehicle.motion.position[0] = 100;
	field.objects[0].state.frame_impact = 1;
	field.objects[0].state.contacts[7].hit = 1;
	field.objects[0].state.contacts[7].normal[1] = 1;
	CHECK(mh_field_advance(&field, &input, 0, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(field.objects[0].state.frame_impact &&
		field.objects[0].state.contacts[7].hit);
	CHECK(mh_field_advance(&field, &input, 1, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(field.objects[0].state.frame_impact &&
		field.objects[0].state.contacts[7].hit);
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE);
	CHECK(field.objects[0].state.frame_impact &&
		field.objects[0].state.contacts[7].hit);
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		!field.objects[0].state.frame_impact);
	field = field_saved;

	/*
	 * Selection uses the shared camera and display origin once per frame.
	 * Moving the camera between slices must neither add nor remove a car.
	 */
	field.cars[0].camera.position[0] = 1000;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 0);
	CHECK(field.object_contact_count[0] == 0 &&
		(field.objects[0].state.flags & 2));
	field.cars[0].camera.position[0] = 0;
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 0);
	CHECK(field.object_contact_count[0] == 0 &&
		(field.objects[0].state.flags & 2));
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 1);
	CHECK(field.object_contact_count[0] != 0 &&
		!(field.objects[0].state.flags & 2));
	field.cars[0] = field_saved.cars[0];
	field.objects[0] = field_saved.objects[0];
	field.cars[0].camera.position[0] = 1000;
	CHECK(mh_field_frame_tick(&field, &input, 0, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 1);
	CHECK(field.object_contact_count[0] == 0 &&
		(field.objects[0].state.flags & 2));
	field = field_saved;
	field.cars[0].camera.position[0] = 1.5f;
	field.cars[0].camera.position[1] = 160.2f;
	field.cars[0].camera.position[2] = 20;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 1);
	field = field_saved;
	field.cars[0].camera.position[0] = 1.5f;
	field.cars[0].camera.position[1] = 160.2f;
	field.cars[0].camera.position[2] = 20;
	memset(field.cars[0].vehicle.motion.rotation, 0,
		sizeof(field.cars[0].vehicle.motion.rotation));
	field.cars[0].vehicle.motion.rotation[0] =
		field.cars[0].vehicle.motion.rotation[4] = -1;
	field.cars[0].vehicle.motion.rotation[8] = 1;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 0);
	field = field_saved;
	field_slow = field_fast = field;
	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
	CHECK(field.contact_count[0] == 1 &&
		field.cars[0].impact_effects.sound_class == 3);
	CHECK(field.object_contact_count[0] == 1);
	CHECK(!(field.objects[0].state.flags & 2) &&
		field.objects[0].state.motion.position[0] < 0);

	for (i = 1; i < 120; i++)
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 30; i++)
		CHECK(mh_field_advance(&field_slow, &input, 1.0 / 30, 0, &steps,
			NULL) == MH_ERROR_NONE);

	for (i = 0; i < 144; i++)
		CHECK(mh_field_advance(&field_fast, &input, 1.0 / 144, 0,
			&steps, NULL) == MH_ERROR_NONE);

	/*
	 * Object integration uses one whole-frame interval, so changing frame
	 * grouping changes the physical trajectory, as in the original.
	 */
	CHECK(memcmp(field.objects, field_slow.objects,
		sizeof(field.objects)) != 0);
	CHECK(memcmp(field.objects, field_fast.objects,
		sizeof(field.objects)) != 0);
	field.objects[0].state.frame_impact = 1;
	field_saved = field;
	CHECK(mh_field_set_objects(&field, &object_source, 1, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&field, &field_saved, sizeof(field)) == 0);
	CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE &&
		field.scene_seconds == field_saved.scene_seconds);
	CHECK(memcmp(field.objects, field_saved.objects,
		sizeof(field.objects)) == 0);
	CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE &&
		field.objects[0].state.flags == 2 && field.scene_seconds == 0);
	CHECK(!field.objects[0].state.frame_impact);
	CHECK(!field.scenery_selection_valid &&
		field.scenery_contact_mask == 0);
	CHECK(memcmp(field.objects[0].state.motion.position,
		object_scene.position,
		sizeof(object_scene.position)) == 0);

	/*
	 * The selected opponent's camera is shared; dormant per-car cameras
	 * must not decide each car's scenery eligibility.
	 */
	CHECK(mh_field_start(&direct, roster, 1, &ai_path, 2, &field, NULL) ==
		MH_ERROR_NONE);
	hulls[1] = hulls[0];
	CHECK(mh_field_set_hulls(&field, hulls, 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_set_objects(&field, &object_source, 1, NULL) ==
		MH_ERROR_NONE);
	field.cars[0].car = &contact_car;

	for (i = 0; i < 2; i++) {
		field.cars[i].countdown = 1;
		field.cars[i].start_ticks = 0;
		field.cars[i].vehicle.motion.position[0] = i ? 1000 : 1.5;
		field.cars[i].vehicle.motion.position[1] = 10;
		field.cars[i].vehicle.motion.position[2] = 20;
	}

	field.cars[0].camera.position[0] = 1000;
	field.cars[1].camera.position[0] = 0;
	field.cars[1].camera.position[1] = 10;
	field.cars[1].camera.position[2] = 20;
	field.viewed_entry = 1;
	CHECK(mh_field_frame_tick(&field, &input, 1, NULL) == MH_ERROR_NONE &&
		field.scenery_contact_mask == 1);
	CHECK(field.object_contact_count[0] != 0 &&
		field.object_contact_count[1] == 0);

	CHECK(mh_field_start(&direct, roster, 7, &ai_path, 2, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.cars[0].ai_path == NULL &&
		memcmp(&field.cars[0].config, &direct.config,
			sizeof(direct.config)) == 0);

	for (i = 0; i < 8; i++)
		for (j = i + 1; j < 8; j++)
			CHECK(field.cars[i].slot != field.cars[j].slot);

	for (i = 0; i < 1440; i++)
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);

	for (j = 0; j < 8; j++)
		CHECK(field.cars[j].start_ticks == 1440 &&
			field.cars[j].ai.state.stuck_seconds == 0);

	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
	field_saved = field;
	CHECK(mh_field_start(&direct, roster, 8, &ai_path, 2, &field, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);

	/*
	 * Timed field: grid order, frozen result samples, recovery and restart.
	 * The real-asset field checker separately earns finishes through
	 * physics.
	 */
	memset(&race_route, 0, sizeof(race_route));
	memset(race_keys, 0, sizeof(race_keys));
	race_route.motion.keys = race_keys;
	race_route.motion.key_count = 65;
	race_route.motion.end_behavior = 2;

	for (i = 0; i < 64; i++) {
		x = i * 6.283185307179586 / 64;
		race_keys[i].time = i * 30;
		race_keys[i].linear = 1;
		race_keys[i].value[0] = 100 * (1 - cos(x));
		race_keys[i].value[1] = .62;
		race_keys[i].value[2] = 100 * sin(x);
	}

	race_keys[64] = race_keys[0];
	race_keys[64].time = 1920;
	track.laps = 1;
	CHECK(mh_drive_start(&car, &col, &materials, &race_route, &track, 3, 0,
		1, &direct, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_start(&direct, roster, 7, &ai_path, 2, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.standings.count == 8 &&
		field.standings.cars[0].position == 4);
	test_shared_camera_recovery(&field, &input);
	test_launch_tuning(&field, &input);
	{
		struct mh_records records, earned;
		struct mh_field fresh, crossing;

		memset(&records, 0, sizeof(records));
		fresh = field;
		CHECK(mh_field_bind_records(&field, &records, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(field.records_bound &&
			field.cars[0].race.record_eligible == 1);

		for (i = 1; i < 8; i++)
			CHECK(field.cars[i].race.record_eligible == 0);

		/*
		 * Progress and record publication happen once at frame end,
		 * using the full frame clock. The lap event remains visible to
		 * the HUD.
		 */
		field.cars[0].countdown = 0;
		field.cars[0].vehicle.motion.position[0] = 0;
		field.cars[0].vehicle.motion.position[2] = 1;
		field.cars[0].race.previous_position[0] = 0;
		field.cars[0].race.previous_position[2] = -1;
		field.cars[0].race.covered = 4;
		field.cars[0].race.group = 3;
		memset(field.cars[0].race.visited, 1, 4);
		crossing = field;
		CHECK(mh_field_advance(&field, &input, 3 * MH_DRIVE_DT, 0,
			&steps, NULL) == MH_ERROR_NONE &&
			steps == 1);
		CHECK(field.records.has_lap && field.records.has_race);
		CHECK(field.cars[0].race.finished &&
			field.cars[0].race.lap_event);
		CHECK(field.cars[0].race.lap_notice == 2 &&
			field.cars[0].race.race_notice == 2);
		CHECK(field.records.lap.milliseconds == 25 &&
			field.records.race.milliseconds == 25);
		earned = field.records;
		field_saved = field;
		field = crossing;
		field.count = 1;
		field.active_mask = 1;
		CHECK(mh_field_advance(&field, &input, 3 * MH_DRIVE_DT, 0,
			&steps, NULL) == MH_ERROR_NONE &&
			steps == 1);
		CHECK(field.records.has_lap && field.records.has_race);
		CHECK(memcmp(&earned, &field.records, sizeof(earned)) == 0);
		field = field_saved;
		CHECK(mh_field_bind_records(&field, &records, 1, NULL) !=
			MH_ERROR_NONE);
		CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE &&
			field.cars[0].race.record_eligible == 0);
		CHECK(memcmp(&earned, &field.records, sizeof(earned)) == 0);
		CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE &&
			field.cars[0].race.record_eligible == 1);

		for (i = 1; i < 8; i++)
			CHECK(field.cars[i].race.record_eligible == 0);

		CHECK(memcmp(&earned, &field.records, sizeof(earned)) == 0);
		field = crossing;
		field.cars[0].race.record_eligible = 0;
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
		CHECK(field.cars[0].race.finished &&
			field.cars[0].race.lap_notice == 1);
		CHECK(!field.records.has_lap && !field.records.has_race);

		/*
		 * A late ranking failure rolls back a record earned in this
		 * tick.
		 */
		field = crossing;
		field.cars[1].countdown = 0;
		field_saved = field;
		race_keys[64].time = 1e9;
		CHECK(mh_field_tick(&field, &input, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
		race_keys[64].time = 1920;
		field = fresh;
		field_saved = field;
		CHECK(mh_field_bind_records(&field, &records, 256, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
		records.has_lap = 2;
		CHECK(mh_field_bind_records(&field, &records, 1, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
	}

	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE &&
		field.standings.cars[0].position == 4);

	for (i = 0; i < 8; i++) {
		field.cars[i].countdown = 0;
		field.cars[i].race.finished = 1;
		field.cars[i].race.best_lap = 1;
		field.cars[i].race.now_ms = field.cars[i].race.lap_start_ms =
			(i + 1) * 1000;
		field.cars[i].race.best_ms = field.cars[i].race.last_ms =
			(i + 1) * 1000;
	}

	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
	CHECK(field.standings.complete && field.standings.finished_count == 8);
	CHECK(field.standings.cars[7].gap_ms == 7000 &&
		field.standings.cars[7].best_ms == 8000);
	{
		struct mh_result_estimate prepared[MH_RACE_MAX_CARS];
		unsigned long seed;

		seed = 1;
		field_saved = field;
		CHECK(mh_field_result_estimates(
			&field, &seed, prepared, NULL) == MH_ERROR_NONE);
		CHECK(seed == 1 && prepared[7].total_ms == 8000);
		CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
		field.cars[1].race.lap = 0;
		CHECK(mh_field_result_estimates(
			&field, &seed, prepared, NULL) == MH_ERROR_NONE);
		CHECK(prepared[1].total_ms == 2000);
		field = field_saved;
	}

	results = field.standings;

	for (i = 0; i < 10; i++)
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);

	CHECK(memcmp(&field.standings, &results, sizeof(results)) == 0);
	CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&field.standings, &results, sizeof(results)) == 0);
	CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE);
	CHECK(!field.standings.complete && field.standings.finished_count == 0);
	CHECK(field.standings.cars[0].position == 4);

	for (i = 0; i < 8; i++) {
		CHECK(field.standings.cars[i].finish_ms == 0 &&
			field.standings.cars[i].best_lap == 0);
	}

	/*
	 * A ranking overflow after all vehicle updates still rolls back the
	 * field.
	 */
	field_saved = field;
	race_keys[64].time = 1e9;
	CHECK(mh_field_tick(&field, &input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field, &field_saved, sizeof(field)) == 0);
	race_keys[64].time = 1920;
	CHECK(mh_drive_start(&car, &col, &materials, &race_route, &track, 63, 0,
		1, &direct, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_start(&direct, NULL, 0, NULL, 2, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(field.standings.cars[0].position == 1 &&
		field.standings.cars[0].score == 1);
	field_saved = field;
	CHECK(mh_field_enable_time_attack(&field, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &field_saved, sizeof(field)) == 0);
	CHECK(mh_drive_start(&car, &col, &materials, &race_route, &track, 0, 0,
		1, &direct, NULL) == MH_ERROR_NONE);
	CHECK(mh_field_start(&direct, NULL, 0, NULL, 2, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_enable_time_attack(&field, NULL) == MH_ERROR_NONE &&
		field.time_attack);
	{
		struct mh_records book;

		memset(&book, 0, sizeof(book));
		book.has_lap = 1;
		book.lap.milliseconds = 10000;
		CHECK(mh_field_bind_records(&field, &book, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE &&
			field.time_attack);
		CHECK(field.cars[0].countdown &&
			field.cars[0].race.now_ms == 0);
		field_saved = field;
		CHECK(mh_field_enable_time_attack(&field, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&field, &field_saved, sizeof(field)) == 0);
		CHECK(mh_field_reset(&field, 0, NULL) == MH_ERROR_NONE &&
			field.time_attack &&
			field.cars[0].race.record_eligible == 0);
		CHECK(mh_field_reset(&field, 1, NULL) == MH_ERROR_NONE &&
			field.time_attack &&
			field.cars[0].race.record_eligible == 1);
		CHECK(field.cars[0].slot == 0 &&
			field.cars[0].race_ticks == 0 &&
			field.cars[0].start_ticks == 0);
		CHECK(field.cars[0].ticks == field_saved.cars[0].ticks);
		CHECK(memcmp(&field.records, &book, sizeof(book)) == 0);
		field.network = 1;
		field_saved = field;
		CHECK(mh_field_enable_time_attack(&field, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&field, &field_saved, sizeof(field)) == 0);
		field.network = 0;
		field.count = 2;
		field.active_mask = 3;
		field_saved = field;
		CHECK(mh_field_enable_time_attack(&field, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&field, &field_saved, sizeof(field)) == 0);
	}

	saved = start;
	car.has_suspension = 0;
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0, 0, 0,
		&start, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&start, &saved, sizeof(start)) == 0);
	mh_route_free(&route);
	mh_collision_free(&col);
	mh_collision_free(&hulls[0]);
}

static void
test_motion(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_motion_state state;
	struct mh_motion_state reset;
	struct mh_motion_forces forces;
	struct mh_motion_result result;
	struct mh_motion_result saved;
	double p[3];
	double r[9];
	double world[3];
	double dt;
	double expected;
	double yaw_length;
	unsigned int i;
	unsigned int j;

	CHECK(mh_collision_col_decode(data, collision_floor_fixture(data, 0),
		&col, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = 1;
	car.mass = 1000;
	car.spring_length = 0.5;
	car.spring_strength = 1000;
	memset(&forces, 0, sizeof(forces));
	memset(r, 0, sizeof(r));
	r[0] = r[4] = r[8] = 1;
	p[0] = p[1] = p[2] = 10;
	CHECK(mh_motion_reset(p, r, &reset, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		NEAR(reset.extension[i], 1);

	dt = 0.06;
	state = reset;
	state.velocity[2] = state.previous_velocity[2] = 6;
	CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result, NULL) ==
		MH_ERROR_NONE);

	/* Original per-axis position stores, rather than exact real sums. */
	NEAR(result.state.position[2], 10.35999965667724609375);
	NEAR(result.state.previous_velocity[2], 6);
	CHECK(mh_motion_step(&car, &col, &result.state, &forces, dt, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.state.position[2], 10.7199993133544921875);

	/*
	 * A history mismatch adds a half-tick correction, saved before motion.
	 */
	state.previous_velocity[2] = 2;
	CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.state.position[2], 10.479999542236328125);
	NEAR(result.state.previous_velocity[2], 6);

	state = reset;
	forces.body_force[1] = -car.mass * 10;
	CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.state.velocity[1], -.599999964237213134765625);
	NEAR(result.state.position[1], 9.9759998321533203125);
	NEAR(result.state.previous_velocity[1], 0);
	CHECK(mh_motion_step(&car, &col, &result.state, &forces, dt, &result,
		NULL) == MH_ERROR_NONE);
	NEAR(result.state.position[1], 9.89799976348876953125);
	NEAR(result.state.previous_velocity[1], -.599999964237213134765625);

	/*
	 * Yaw preserves WORLD velocity through basis changes. Translation uses
	 * the newly rotated basis before local velocity is rebased.
	 */
	memset(&forces, 0, sizeof(forces));
	state = reset;
	state.velocity[2] = state.previous_velocity[2] = 6;
	state.angular_velocity[1] = state.previous_angular_velocity[1] = 2;
	CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result, NULL) ==
		MH_ERROR_NONE);

	/* Three passes use the stored binary32 dt/3. */
	expected = 6 * (double)(float)((double)(float)dt / 3);

	/* Normalize the expected binary32 rotation components. */
	yaw_length = sqrt(
		.992808640003204345703125 * .992808640003204345703125 +
		.1197122037410736083984375 * .1197122037410736083984375);
	NEAR(result.state.rotation[0], .992808640003204345703125 / yaw_length);
	NEAR(result.state.rotation[2],
		-.1197122037410736083984375 / yaw_length);
	NEAR(result.state.position[0], 10.01439666748046875);
	NEAR(result.state.position[2], 10.35971164703369140625);
	CHECK(mh_motion_display(&result.state, p, world, NULL) ==
		MH_ERROR_NONE);

	/* Each pass now rounds local velocity before its basis change. */
	CHECK(fabs(world[0]) < 6 * FLT_EPSILON);
	CHECK(fabs(world[2] - 6) < 6 * FLT_EPSILON);
	NEAR(p[1], 10 + 0.3199999928474426);

	/*
	 * The correction rotates too, but does not integrate angular velocity.
	 */
	state.previous_angular_velocity[1] = 0;
	CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.state.rotation[0], cos(expected + dt));
	NEAR(result.state.angular_velocity[1], 2);
	NEAR(result.state.previous_angular_velocity[1], 2);

	for (i = 0; i < 3; i++) {
		state = reset;
		memset(&forces, 0, sizeof(forces));
		forces.body_torque[i] = i == 0 ? 3500 : (i == 1 ? 2000 : 1500);
		CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result,
			NULL) == MH_ERROR_NONE);
		NEAR(result.state.angular_velocity[i],
			.0599999986588954925537109375);

		/*
		 * Original stores give .02f, .04f, .06f for the three speeds.
		 */
		expected = (.0199999995529651641845703125 +
				.039999999105930328369140625 +
				.0599999986588954925537109375) *
			.0199999995529651641845703125;
		NEAR(result.state.rotation[((i + 1) % 3) * 3 + (i + 1) % 3],
			cos(expected));
		NEAR(result.state.rotation[((i + 1) % 3) * 3 + (i + 2) % 3],
			sin(expected));
		CHECK(mh_motion_display(&result.state, p, world, NULL) ==
			MH_ERROR_NONE);

		for (j = 0; j < 3; j++)
			NEAR(p[j],
				10 +
					0.3199999928474426 *
						result.state.rotation[3 + j]);
	}

	memset(&forces, 0, sizeof(forces));

	for (i = 0; i < 6; i++) {
		state = reset;
		state.velocity[0] = state.previous_velocity[0] =
			i < 2 ? 0.6999 : (i < 4 ? -0.7 : 0.7);
		state.velocity[2] = state.previous_velocity[2] =
			(i & 1) ? -3 : 2.9999;
		CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result,
			NULL) == MH_ERROR_NONE);
		NEAR(result.state.velocity[0],
			(i & 1) ? (double)(float)state.velocity[0] : 0);
	}

	/* Many rotations retain an orthonormal, right-handed basis. */
	state = reset;
	state.angular_velocity[0] = state.previous_angular_velocity[0] = 0.21;
	state.angular_velocity[1] = state.previous_angular_velocity[1] = -0.37;
	state.angular_velocity[2] = state.previous_angular_velocity[2] = 0.13;

	for (i = 0; i < 4000; i++) {
		CHECK(mh_motion_step(&car, &col, &state, &forces, 1.0 / 60.0,
			&result, NULL) == MH_ERROR_NONE);
		state = result.state;
	}

	for (i = 0; i < 3; i++) {
		NEAR(state.rotation[i * 3] * state.rotation[i * 3] +
				state.rotation[i * 3 + 1] *
					state.rotation[i * 3 + 1] +
				state.rotation[i * 3 + 2] *
					state.rotation[i * 3 + 2],
			1);
	}

	/*
	 * Coupled drop, landing and settling, using all three passes and
	 * history correction. Gravity=10 is a supplied test load.
	 */
	state = reset;
	state.position[0] = state.position[2] = 0;
	state.position[1] = 2.55;
	car.spring_strength = 100000;

	for (i = 0; i < 4; i++) {
		car.wheel_position[i][0] = (i == 0 || i == 3) ? -0.4 : 0.4;
		car.wheel_position[i][2] = i < 2 ? 0.7 : -0.7;
	}

	forces.body_force[1] = -car.mass * 10;
	dt = 1.0 / 60.0;

	for (i = 0; i < 1800; i++) {
		CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result,
			NULL) == MH_ERROR_NONE);
		state = result.state;
	}

	expected = (sqrt(0.01 +
			car.mass * 10 /
				(car.spring_length * car.spring_strength)) -
			0.1) /
		2;
	CHECK(fabs(state.position[1] -
		(2 + car.spring_length * (1 - expected))) <
		FLT_EPSILON * fabs(state.position[1]));

	/*
	 * Cached binary32 endpoints leave a small damping oscillation at rest.
	 * Translation stores occur on each of the three passes. Travel per
	 * pass remains below the pose's binary32 resolution.
	 */
	CHECK(fabs(state.velocity[1]) * dt / MH_MOTION_PASSES <
		FLT_EPSILON * fabs(state.position[1]));

	for (i = 0; i < 3; i++)

		for (j = 0; j < 4; j++)
			CHECK(result.passes[i].wheels[j].supported);

	memset(&forces, 0, sizeof(forces));
	saved = result;
	state = reset;
	CHECK(mh_motion_step(&car, &col, &state, &forces, 0, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_motion_step(&car, &col, &state, &forces, -0.01, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_motion_step(&car, &col, &state, &forces, 0.10001, &result,
		NULL) != MH_ERROR_NONE);

	for (i = 0; i < 6; i++) {
		state = reset;

		if (i == 0)
			state.rotation[0] = -1;

		if (i == 1)
			state.velocity[1] = HUGE_VAL;

		if (i == 2)
			state.previous_angular_velocity[2] = DBL_MAX;

		if (i == 3)
			state.extension[2] = -0.01;

		/*
		 * First pass is valid, subsequent motion exceeds coordinate
		 * limit.
		 */
		if (i == 4) {
			state.position[0] = 999999;
			state.velocity[0] = state.previous_velocity[0] = 100;
		}

		if (i == 5)
			forces.body_torque[1] = DBL_MAX;

		CHECK(mh_motion_step(&car, &col, &state, &forces, dt, &result,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	}

	CHECK(mh_motion_reset(NULL, r, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_motion_display(NULL, p, world, NULL) != MH_ERROR_NONE);
	mh_collision_free(&col);
}

static void
test_suspension(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	struct mh_collision col;
	struct mh_car car;
	struct mh_suspension_input input;
	struct mh_suspension output;
	struct mh_suspension saved;
	struct mh_suspension_config config;
	size_t size;
	unsigned int i;
	double c;
	double expected[3];
	double velocity;
	double compression;
	unsigned int step;

	size = collision_floor_fixture(data, 0);
	CHECK(mh_collision_col_decode(data, size, &col, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	car.has_suspension = 1;
	car.mass = 1000;
	car.spring_length = 0.5;
	car.spring_strength = 1000;
	memset(&input, 0, sizeof(input));
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.position[1] = 2.25;

	for (i = 0; i < 4; i++) {
		car.wheel_position[i][0] = (i == 0 || i == 3) ? -0.5 : 0.5;
		car.wheel_position[i][2] = i < 2 ? 0.5 : -0.5;
		input.previous_extension[i] = 0.5;
	}

	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	NEAR(output.force[1], 600);

	for (i = 0; i < 4; i++) {
		CHECK(output.wheels[i].supported &&
			output.wheels[i].contact.material == 2);
		NEAR(output.wheels[i].extension, 0.5);
		NEAR(output.wheels[i].spring_force, 150);
		NEAR(output.wheels[i].damper_force, 0);
		NEAR(output.wheels[i].traction_scale, 1);
		NEAR(output.wheels[i].start[0], car.wheel_position[i][0]);
		NEAR(output.wheels[i].start[1], 2.75);
		NEAR(output.wheels[i].end[1], 1.75);
		input.previous_extension[i] = 1;
	}

	for (i = 0; i < 3; i++)
		NEAR(output.torque[i], 0);

	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	NEAR(output.wheels[0].damper_force, 32500);
	NEAR(output.force[1], 130600);

	/*
	 * Hidden settings change casts, spring forces and the SIGN of damping.
	 * All four wheels must consume them, while CAR settings stay immutable.
	 */
	CHECK(mh_suspension_configure(&car, MH_CHEAT_DEMON, &config, NULL) ==
		MH_ERROR_NONE);
	input.position[1] = 2.75;
	CHECK(mh_suspension_evaluate_configured(&car, &col, &config, &input,
		&output, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		NEAR(output.wheels[i].extension, .5);
		NEAR(output.wheels[i].spring_force, 72);
		NEAR(output.wheels[i].damper_force, 10833);
	}

	CHECK(mh_suspension_configure(&car, MH_CHEAT_G_RIDE, &config, NULL) ==
		MH_ERROR_NONE);
	input.position[1] = 2 + config.length * .5;
	CHECK(mh_suspension_evaluate_configured(&car, &col, &config, &input,
		&output, NULL) == MH_ERROR_NONE);

	/*
	 * Original endpoint stores: Y=2.9000000953674316 / 1.6999999284744263.
	 */
	/* The force consumer reads t as binary32: .75, hence extension .5. */
	for (i = 0; i < 4; i++) {
		NEAR(output.wheels[i].extension, .5);
		NEAR(output.wheels[i].spring_force, 90.0000035762786865234375);
		NEAR(output.wheels[i].damper_force, -25000);
	}

	CHECK(car.spring_length == .5 && car.spring_strength == 1000);
	saved = output;
	config.damping = DBL_MAX;
	CHECK(mh_suspension_evaluate_configured(&car, &col, &config, &input,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);

	/* Full extension at an endpoint contact, zero spring/damping. */
	input.position[1] = 2.5;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.wheels[0].supported && output.wheels[0].extension == 1);
	NEAR(output.force[1], 0);

	/* t=.5 is rejected; crossing above an attachment is rejected too. */
	input.position[1] = 2;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.wheels[0].contact.hit && !output.wheels[0].supported);
	NEAR(output.wheels[0].extension, 0.5);
	NEAR(output.force[1], 0);
	input.position[1] = 1.875;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.wheels[0].contact.hit && !output.wheels[0].supported);
	NEAR(output.wheels[0].extension, 0.5);

	/*
	 * Applied force still acts on an unsupported/airborne wheel; traction
	 * and damping do not. The moment arm uses the unscaled attachment.
	 */
	input.traction[0][0] = 100;
	input.applied[0][1] = 10;
	input.position[1] = 4;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(!output.wheels[0].contact.hit && !output.wheels[0].supported);
	NEAR(output.wheels[0].extension, 1);
	NEAR(output.force[0], 0);
	NEAR(output.force[1], 10);
	NEAR(output.torque[0], -5);
	NEAR(output.torque[2], -5);
	saved = output;
	input.position[0] =
		1e6; /* First wheel inside, second outside cast limit. */
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	input.position[0] = 0;
	input.rotation[0] = -1; /* A reflection is not a car rotation. */
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) !=
		MH_ERROR_NONE);
	input.rotation[0] = 1;
	input.previous_extension[3] = -0.1;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) !=
		MH_ERROR_NONE);
	input.previous_extension[3] = 1;
	car.spring_length = 0;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) !=
		MH_ERROR_NONE);
	car.spring_length = 0.5;
	input.applied[3][2] = DBL_MAX;
	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);

	/*
	 * A banked car has a body-space normal distinct from world up.
	 * Test tangent projection, damping sign, and center-of-mass torque.
	 */
	memset(&input, 0, sizeof(input));
	memset(car.wheel_position, 0, sizeof(car.wheel_position));
	car.center_mass[2] = 0.25;
	c = sqrt(0.75);
	input.rotation[0] = input.rotation[4] = c;
	input.rotation[1] = 0.5;
	input.rotation[3] = -0.5;
	input.rotation[8] = 1;
	input.position[1] = 2 + 0.25 * c;

	for (i = 0; i < 4; i++) {
		input.previous_extension[i] = 0.25;
		input.traction[i][0] = 8;
		input.traction[i][1] = 4;
		input.traction[i][2] = -6;
	}

	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);

	/* Wheel forces with stored t and projected normal. */
	expected[0] = 3.2009618282318115234375;
	expected[1] = -16101.83203125;
	expected[2] = -4.5;
	NEAR(output.wheels[0].extension, .4999997615814208984375);
	NEAR(output.wheels[0].traction_scale, .749999973077653);
	NEAR(output.wheels[0].damper_force, -16249.9845027923583984375);

	for (i = 0; i < 3; i++)
		NEAR(output.wheels[0].force[i], expected[i]);

	NEAR(output.wheels[0].torque[0], .25 * expected[1]);
	NEAR(output.wheels[0].torque[1], -.25 * expected[0]);
	NEAR(output.wheels[0].torque[2], 0);

	/*
	 * Constrained vertical test rig: fixed 1/180 second semi-implicit
	 * steps, artificial gravity=10, rotation locked. This is not a game
	 * integrator. Reusing returned extension must settle to the analytic
	 * force balance.
	 */
	memset(&input, 0, sizeof(input));
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.position[1] = 2.7;
	car.mass = 100;

	for (i = 0; i < 4; i++) {
		input.previous_extension[i] = 1;
		input.applied[i][1] = -250;
	}

	velocity = 0;

	for (step = 0; step < 6000; step++) {
		CHECK(mh_suspension_evaluate(&car, &col, &input, &output,
			NULL) == MH_ERROR_NONE);
		velocity += output.force[1] / car.mass / 180;
		input.position[1] += velocity / 180;

		for (i = 0; i < 4; i++)
			input.previous_extension[i] =
				output.wheels[i].extension;
	}

	compression =
		(sqrt(.01 + 1000 / (car.spring_length * car.spring_strength)) -
			.1) /
		2;

	/* Stored endpoints quantize extension and the per-pass damper force. */
	CHECK(fabs(velocity) / 180 < FLT_EPSILON * fabs(input.position[1]));
	CHECK(fabs(input.position[1] -
		(2 + car.spring_length * (1 - compression))) < 1e-7);
	CHECK(output.wheels[0].supported);
	CHECK(fabs(output.force[1]) <
		4 * (65000 + 2.1 * car.spring_length * car.spring_strength) *
			FLT_EPSILON * fabs(input.position[1]) /
			car.spring_length);
	mh_collision_free(&col);
}

static void
test_collision_queries(void)
{
	unsigned char data[FLOOR_FIXTURE_SIZE];
	unsigned char copy[FLOOR_FIXTURE_SIZE];
	struct mh_col col;
	struct mh_collision collision;
	struct mh_col saved;
	struct mh_col_polygon polygon;
	struct mh_contact hit;
	struct mh_contact saved_hit;
	size_t size;
	size_t n;
	unsigned long cell[3];
	unsigned long count;
	unsigned long seed;
	const unsigned char *list;
	enum mh_error_code status;
	double start[3] = {0, 3, 0};
	double end[3] = {0, -1, 0};
	int i;

	memset(&collision, 0, sizeof(collision));
	size = collision_floor_fixture(data, 0);
	memcpy(copy, data, sizeof(data));
	CHECK(mh_col_parse(data, size, &col, NULL) == MH_ERROR_NONE);
	mh_collision_free(&collision);
	CHECK(mh_collision_col_decode(data, size, &collision, NULL) ==
		MH_ERROR_NONE);
	CHECK(col.polygon_count == 2 && col.boundary_count == 4 &&
		col.root_count == 1);
	CHECK(mh_col_polygon(&col, 1, &polygon) && polygon.material == 2);
	CHECK(polygon.boundary_count == 4 && polygon.boundaries[1][0] == -1);
	CHECK(!mh_col_polygon(&col, 2, &polygon) && polygon.material == 2);
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	CHECK(hit.polygon == 1 && hit.material == 2 && hit.polygon_tests == 2);
	NEAR(hit.fraction, 0.25);
	NEAR(hit.position[1], 2);
	CHECK(hit.normal[1] == 1);
	CHECK(mh_collision_cast(&collision, end, start, &hit, NULL) ==
			MH_ERROR_NONE &&
		!hit.hit);
	CHECK(mh_collision_cast(&collision, start, start, &hit, NULL) ==
			MH_ERROR_NONE &&
		!hit.hit);
	start[0] = end[0] = 1.0005;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	start[0] = end[0] = 1.002;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		!hit.hit);
	start[0] = end[0] = 0;
	start[1] = -0.04;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	NEAR(hit.position[1], 0);
	CHECK(hit.fraction == 0.0416666641831398);
	start[1] = -0.08;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		!hit.hit);
	start[1] = 0.04;
	end[1] = 0.02;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	CHECK(hit.fraction == 0.4000000059604645);
	CHECK(hit.position[1] == 0.03199999779462814);
	saved_hit = hit;
	start[0] = DBL_MAX;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&hit, &saved_hit, sizeof(hit)) == 0);
	start[0] = 0;
	start[1] = 10;
	end[1] = -10;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	CHECK(memcmp(data, copy, sizeof(data)) == 0);

	/*
	 * Exactly representable ties retain the first leaf-list entry. A
	 * segment ending exactly on the plane still hits.
	 */
	collision_put32(data + col.polygon_offset + 28 + 24, 0);
	collision_put16(data + col.list_offset + 6, 1);
	collision_put16(data + col.list_offset + 8, 0);
	CHECK(mh_col_parse(data, size, &col, NULL) == MH_ERROR_NONE);
	mh_collision_free(&collision);
	CHECK(mh_collision_col_decode(data, size, &collision, NULL) ==
		MH_ERROR_NONE);
	end[1] = 0;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	CHECK(hit.polygon == 1 && hit.material == 2 && hit.fraction == 1);
	end[1] = -10;
	size = collision_floor_fixture(data, 1);
	CHECK(mh_col_parse(data, size, &col, NULL) == MH_ERROR_NONE);
	mh_collision_free(&collision);
	CHECK(mh_collision_col_decode(data, size, &collision, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
			MH_ERROR_NONE &&
		hit.hit);
	NEAR(hit.position[1], 2);

	for (i = 0; i < 8; i++) {

		cell[0] = (unsigned long)((i >> 2) & 1);
		cell[1] = (unsigned long)((i >> 1) & 1);
		cell[2] = (unsigned long)(i & 1);
		list = mh_col_cell(&col, cell, &count);
		CHECK(count == (i == 5 ? 0UL : 2UL));

		if (count != 0)
			CHECK(list != NULL && mh_read_u16le(list + 2) == 1);
	}

	cell[0] = 2;
	CHECK(mh_col_cell(&col, cell, &count) == NULL && count == 0);
	saved = col;

	for (n = 0; n < size; n++) {
		CHECK(mh_col_parse(data, n, &col, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&col, &saved, sizeof(col)) == 0);
	}

	CHECK(mh_col_parse(data, size + 1, &col, NULL) != MH_ERROR_NONE);

	for (i = 0; i < 14; i++) {
		size = collision_floor_fixture(data, 1);

		if (i == 0)
			collision_put16(data + 58, 1);

		if (i == 1)
			collision_put16(data + 58, 2);

		if (i == 2)
			collision_put16(data + 58, 0x8002);

		if (i == 3)
			collision_put16(data + 58, 0x8000);

		if (i == 4)
			collision_put16(data + 78, 100);

		if (i == 5)
			collision_put16(data + 80, 2);

		if (i == 6)
			collision_put16(data + 88, 9);

		if (i == 7)
			collision_put32(data + 98, 0x7f800000UL);

		if (i == 8)
			collision_put32(data + 32, 0);

		if (i == 9)
			collision_put16(data, 44);

		if (i == 10)
			collision_put16(data + 2, 16);

		if (i == 11)
			collision_put16(data + 6, 1);

		if (i == 12)
			collision_put32(data + 102, 0x40000000UL);

		if (i == 13)
			collision_put16(data + 88, 0);

		CHECK(mh_col_parse(data, size, &col, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&col, &saved, sizeof(col)) == 0);
	}

	/* Ignore scratch after a triangle's zero terminator. */
	size = collision_floor_fixture(data, 1);
	collision_put16(data + 94, 0);
	collision_put16(data + 96, MH_U16_MAX);
	CHECK(mh_col_parse(data, size, &col, NULL) == MH_ERROR_NONE);
	mh_collision_free(&collision);
	CHECK(mh_collision_col_decode(data, size, &collision, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_col_polygon(&col, 0, &polygon) && polygon.boundary_count == 3);
	seed = 211;

	for (i = 0; i < 4000; i++) {
		size = collision_floor_fixture(data, 1);
		seed = (seed * 1664525UL + 1013904223UL) & MH_U32_MAX;
		collision_put32(data + seed % (size - 3), seed);
		col = saved;
		status = mh_col_parse(data, size, &col, NULL);

		if (status != MH_ERROR_NONE)
			CHECK(memcmp(&col, &saved, sizeof(col)) == 0);
		else {
			mh_collision_free(&collision);
			CHECK(mh_collision_col_decode(data, size, &collision,
				NULL) == MH_ERROR_NONE);

			/*
			 * Corrupt translations may exceed signed integer grid
			 * range.
			 */
			saved_hit = hit;
			status = mh_collision_cast(
				&collision, start, end, &hit, NULL);
			CHECK(status == MH_ERROR_NONE ||
				status == MH_ERROR_INVALID);

			if (status != MH_ERROR_NONE)
				CHECK(memcmp(&hit, &saved_hit, sizeof(hit)) ==
					0);
		}
	}

	mh_collision_free(&collision);
}

/* Directly authored data exercises physics without a COL file or decoder. */
static void
test_authored_collision(void)
{
	struct mh_collision collision;
	struct mh_collision_cell cell;
	struct mh_collision_leaf leaf;
	struct mh_collision_polygon polygons[2];
	struct mh_contact hit;
	unsigned long indices[3] = {1, 0, 1};
	double start[3] = {0, 3, 0}, end[3] = {0, -1, 0};
	unsigned int i;

	memset(&collision, 0, sizeof(collision));
	memset(polygons, 0, sizeof(polygons));
	cell.kind = MH_COLLISION_LEAF;
	cell.index = 0;
	leaf.first = 0;
	leaf.count = 3;

	for (i = 0; i < 3; i++) {
		collision.dimensions[i] = collision.roots[i] = 1;
		collision.translation[i] = 4;
	}

	collision.scale = .125;
	collision.cell_size = 8;
	collision.root_count = collision.leaf_count = 1;
	collision.polygon_count = 2;
	collision.polygon_index_count = 3;
	collision.cells = &cell;
	collision.leaves = &leaf;
	collision.polygons = polygons;
	collision.polygon_indices = indices;
	polygons[0].material = 7;
	polygons[0].plane[1] = 1;
	polygons[0].boundary_count = 4;
	polygons[0].boundaries[0][0] = 1;
	polygons[0].boundaries[1][0] = -1;
	polygons[0].boundaries[2][2] = 1;
	polygons[0].boundaries[3][2] = -1;

	for (i = 0; i < 4; i++)
		polygons[0].boundaries[i][3] = 1;

	polygons[1] = polygons[0];
	polygons[1].material = 3;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
		MH_ERROR_NONE);
	CHECK(hit.hit && hit.polygon == 1 && hit.material == 3 &&
		hit.polygon_tests == 3 && hit.fraction == .75);
	polygons[1].plane[3] = -2;
	CHECK(mh_collision_cast(&collision, start, end, &hit, NULL) ==
		MH_ERROR_NONE);
	CHECK(hit.hit && hit.polygon == 1 && hit.material == 3 &&
		hit.polygon_tests == 3 && hit.fraction == .25);
}

static void
test_collision_selection(void)
{
	struct mh_collision col;
	struct mh_collision_cell cells[4];
	struct mh_collision_leaf leaves[2];
	struct mh_collision_polygon polygons[2];
	struct mh_contact hit, saved;
	unsigned long indices[3] = {1, 0, 1};
	double start[3] = {.5, .5, .5}, end[3] = {.5, -.5, .5};
	unsigned int i;

	memset(&col, 0, sizeof(col));
	memset(polygons, 0, sizeof(polygons));
	col.dimensions[0] = col.roots[0] = 4;
	col.dimensions[1] = col.dimensions[2] = 1;
	col.roots[1] = col.roots[2] = 1;
	col.scale = col.cell_size = 1;
	col.root_count = 4;
	col.leaf_count = col.polygon_count = 2;
	col.polygon_index_count = 3;
	col.cells = cells;
	col.leaves = leaves;
	col.polygons = polygons;
	col.polygon_indices = indices;
	leaves[0].first = 0;
	leaves[0].count = 2;
	leaves[1].first = 2;
	leaves[1].count = 1;

	for (i = 0; i < 4; i++) {
		cells[i].kind = MH_COLLISION_LEAF;
		cells[i].index = 0;
	}

	for (i = 0; i < 2; i++) {
		polygons[i].material = 7 + 2 * i;
		polygons[i].plane[1] = 1;
		polygons[i].boundary_count = 3;
		polygons[i].boundaries[0][0] = 1;
		polygons[i].boundaries[1][0] = -1;
		polygons[i].boundaries[2][2] = 1;
		polygons[i].boundaries[0][3] = 10;
		polygons[i].boundaries[1][3] = 10;
		polygons[i].boundaries[2][3] = 10;
	}

	/* Exact .5 ties keep the first leaf-list entry. */
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 9 && hit.fraction == .5);
	indices[0] = 0;
	indices[1] = 1;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 7 && hit.fraction == .5);

	/* A quotient of 1/3 is less than its stored winner: last entry wins. */
	start[1] = 1;
	end[1] = -2;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 9 &&
		hit.fraction == 0.3333333432674408);
	indices[0] = 1;
	indices[1] = 0;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 7 &&
		hit.fraction == 0.3333333432674408);

	/*
	 * First accepted cell wins even when another cell has a nearer plane.
	 */
	leaves[0].count = 1;
	indices[0] = 0;

	for (i = 1; i < 4; i++)
		cells[i].index = 1;

	polygons[1].plane[3] = (float)-.2;
	start[1] = .6;
	end[0] = 1.5;
	end[1] = -.4;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 7 &&
		hit.fraction == 0.6000000238418579);
	CHECK(hit.position[0] == 1.100000023841858 && hit.polygon_tests == 1);
	cells[0].kind = MH_COLLISION_EMPTY;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 9 &&
		hit.fraction == 0.4000000059604645);

	/*
	 * Strict interior crossings exclude the cell beyond an exact endpoint.
	 */
	end[0] = 1;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(!hit.hit && hit.polygon_tests == 0);

	/*
	 * Skip millions of outside cells while preserving the first grid cell.
	 */
	cells[0].kind = MH_COLLISION_LEAF;
	start[0] = -1e6;
	end[0] = 1e6;
	polygons[0].boundaries[0][3] = polygons[0].boundaries[1][3] = 1e6;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) == MH_ERROR_NONE);
	CHECK(hit.hit && hit.material == 7 && hit.polygon_tests == 1);

	/* A valid COL may place its grid outside the original integer range. */
	saved = hit;
	col.translation[0] = 1e30;
	CHECK(mh_collision_cast(&col, start, end, &hit, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&hit, &saved, sizeof(hit)) == 0);
}

int
main(void)
{
	test_authored_collision();
	test_collision_selection();
	test_suspension();
	test_motion();
	test_collision_queries();
	test_query_stores();

	if (failures != 0)
		return 1;

	test_body_endpoint_storage();
	test_body();
	test_body_response_stores();
	test_object_ground_frame();
	test_grip_motion();
	test_vehicle();
	test_drive();

	if (failures != 0)
		return 1;

	puts("COL pools/octrees, road contacts, suspension, motion and material ordering passed");

	return 0;
}
