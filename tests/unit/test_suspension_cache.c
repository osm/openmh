#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/simulation/motion.h"
#include "engine/simulation/suspension_internal.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
fixture(struct mh_collision *col, struct mh_collision_cell *cell,
	struct mh_collision_leaf *leaf, unsigned long indices[2],
	struct mh_collision_polygon polygons[2])
{
	unsigned int axis;

	memset(col, 0, sizeof(*col));
	memset(polygons, 0, 2 * sizeof(*polygons));
	cell->kind = MH_COLLISION_LEAF;
	cell->index = 0;
	leaf->first = 0;
	leaf->count = 2;
	indices[0] = 0;
	indices[1] = 1;
	col->cells = cell;
	col->leaves = leaf;
	col->polygons = polygons;
	col->polygon_indices = indices;
	col->root_count = col->leaf_count = 1;
	col->polygon_count = col->polygon_index_count = 2;
	col->scale = .125;
	col->cell_size = 8;

	for (axis = 0; axis < 3; axis++) {
		col->translation[axis] = 4;
		col->roots[axis] = col->dimensions[axis] = 1;
	}

	/* Adjacent surfaces: y=0 at x<=1, y=.2 at x>=1. */
	polygons[0].material = 7;
	polygons[1].material = 9;
	polygons[0].plane[1] = polygons[1].plane[1] = 1;
	polygons[1].plane[3] = (double)(float)-.2;
	polygons[0].boundary_count = polygons[1].boundary_count = 1;
	polygons[0].boundaries[0][0] = -1;
	polygons[0].boundaries[0][3] = 1;
	polygons[1].boundaries[0][0] = 1;
	polygons[1].boundaries[0][3] = -1;
}

static void
test_contact(const struct mh_collision *col)
{
	struct mh_contact first, cached, fresh;
	double start[3] = {.5, .6, .5};
	double end[3] = {.5, -.4, .5};

	CHECK(mh_collision_cast(col, start, end, &first, NULL) ==
		MH_ERROR_NONE);
	CHECK(first.hit && first.material == 7);
	start[0] = end[0] = 1.02;
	CHECK(mh_collision_cast(col, start, end, &fresh, NULL) ==
		MH_ERROR_NONE);
	cached = first;
	mh_suspension_reuse_contact(start, end, &cached);
	CHECK(fresh.hit && fresh.material == 9);
	CHECK(cached.hit && cached.material == 7 &&
		cached.polygon == first.polygon);

	/* Synthetic fixture: stored binary32 fraction. */
	CHECK(cached.fraction == .60000002384185791015625);
	CHECK(memcmp(cached.position, first.position, sizeof(first.position)) ==
		0);
	CHECK(memcmp(cached.normal, first.normal, sizeof(first.normal)) == 0);
	CHECK(cached.plane_distance == first.plane_distance &&
		cached.polygon_tests == 0);

	end[1] = .1;
	CHECK(mh_collision_cast(col, start, end, &fresh, NULL) ==
		MH_ERROR_NONE);
	CHECK(fresh.hit && fresh.material == 9 && fresh.fraction > .5);
	mh_suspension_reuse_contact(start, end, &cached);
	CHECK(!cached.hit && cached.material == 7);
	CHECK(cached.fraction == .60000002384185791015625);

	/*
	 * Binary64 tolerance comparison accepts binary32(.08), but not its
	 * successor.
	 */
	end[1] = .07999999821186065673828125;
	mh_suspension_reuse_contact(start, end, &cached);
	CHECK(cached.hit && cached.fraction == 1);
	end[1] = .080000005662441253662109375;
	mh_suspension_reuse_contact(start, end, &cached);
	CHECK(!cached.hit && cached.fraction == 1);
	start[1] = -.07999999821186065673828125;
	end[1] = -.2;
	mh_suspension_reuse_contact(start, end, &cached);
	CHECK(!cached.hit);
}

static void
test_suspension(const struct mh_collision *col, const struct mh_car *car)
{
	struct mh_suspension_input input;
	struct mh_suspension first, reused, fresh, saved;
	struct mh_suspension_config config;
	unsigned int wheel;

	memset(&input, 0, sizeof(input));
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.position[0] = .5;
	input.position[1] = .1;

	for (wheel = 0; wheel < 4; wheel++)
		input.previous_extension[wheel] = .2;

	CHECK(mh_suspension_evaluate(car, col, &input, &first, NULL) ==
		MH_ERROR_NONE);
	input.position[0] = 1.02;
	CHECK(mh_suspension_evaluate(car, col, &input, &fresh, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_suspension_evaluate_cached(car, col, NULL, &input, &first,
		&reused, NULL) == MH_ERROR_NONE);

	for (wheel = 0; wheel < 4; wheel++) {
		CHECK(reused.wheels[wheel].supported &&
			reused.wheels[wheel].contact.material == 7);
		CHECK(fresh.wheels[wheel].contact.material == 9 &&
			!fresh.wheels[wheel].supported);
	}

	/* A preceding hit without support must use the full query. */
	first.wheels[0].supported = 0;
	CHECK(mh_suspension_evaluate_cached(car, col, NULL, &input, &first,
		&reused, NULL) == MH_ERROR_NONE);
	CHECK(reused.wheels[0].contact.material == 9);
	CHECK(reused.wheels[1].contact.material == 7);
	first.wheels[0].supported = 1;

	/* A cached miss does not fall back to the available raised surface. */
	config.length = .25;
	config.strength = config.damping = 0;
	input.position[1] = .35;
	CHECK(mh_suspension_evaluate_cached(car, col, &config, &input, &first,
		&reused, NULL) == MH_ERROR_NONE);
	CHECK(mh_suspension_evaluate_configured(car, col, &config, &input,
		&fresh, NULL) == MH_ERROR_NONE);
	CHECK(!reused.wheels[0].contact.hit && !reused.wheels[0].supported &&
		reused.wheels[0].extension == 1);
	CHECK(fresh.wheels[0].supported &&
		fresh.wheels[0].contact.material == 9);

	/* Cached queries retain endpoint bounds and failure atomicity. */
	saved = reused;
	input.position[1] = 1e6;
	CHECK(mh_suspension_evaluate_cached(car, col, &config, &input, &first,
		&reused, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &reused, sizeof(saved)) == 0);
}

static void
test_endpoints(void)
{
	struct mh_collision col;
	struct mh_car car;
	struct mh_suspension_input input;
	struct mh_suspension output;
	unsigned int wheel;

	memset(&col, 0, sizeof(col));
	memset(&car, 0, sizeof(car));
	memset(&input, 0, sizeof(input));
	car.has_suspension = 1;
	car.mass = 1000;
	car.spring_length = .275;
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.position[0] = 1024;
	input.position[1] = .1;
	input.position[2] = -.3;

	for (wheel = 0; wheel < 4; wheel++) {
		car.wheel_position[wheel][0] = .1;
		car.wheel_position[wheel][1] = .2;
		car.wheel_position[wheel][2] = .3;
	}

	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);

	/* Synthetic fixtures for fresh and cached-plane queries. */
	for (wheel = 0; wheel < 4; wheel++) {
		CHECK(output.wheels[wheel].start[0] == 1024.0999755859375);
		CHECK(output.wheels[wheel].end[0] == 1024.0999755859375);
		CHECK(output.wheels[wheel].start[1] == .5750000476837158203125);
		CHECK(output.wheels[wheel].end[1] ==
			.0250000059604644775390625);
		CHECK(output.wheels[wheel].start[2] == 0);
		CHECK(output.wheels[wheel].end[2] == 0);
	}

	input.rotation[0] = input.rotation[8] = .6;
	input.rotation[2] = -.8;
	input.rotation[6] = .8;
	input.position[0] = 1024.3;
	input.position[1] = .199999;
	input.position[2] = -512.17;

	for (wheel = 0; wheel < 4; wheel++) {
		car.wheel_position[wheel][0] = .6;
		car.wheel_position[wheel][1] = .63;
		car.wheel_position[wheel][2] = 1.05;
	}

	CHECK(mh_suspension_evaluate(&car, &col, &input, &output, NULL) ==
		MH_ERROR_NONE);

	for (wheel = 0; wheel < 4; wheel++) {
		CHECK(output.wheels[wheel].start[0] == 1025.5);
		CHECK(output.wheels[wheel].end[0] == 1025.5);
		CHECK(output.wheels[wheel].start[1] ==
			1.10499894618988037109375);
		CHECK(output.wheels[wheel].end[1] == .55499899387359619140625);
		CHECK(output.wheels[wheel].start[2] == -512.01995849609375);
		CHECK(output.wheels[wheel].end[2] == -512.01995849609375);
	}
}

static void
test_passes(const struct mh_collision *col, const struct mh_car *car)
{
	struct mh_motion_state input;
	struct mh_motion_result result, next;
	struct mh_motion_forces forces;
	double position[3] = {.5, .1, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int wheel;

	memset(&forces, 0, sizeof(forces));
	CHECK(mh_motion_reset(position, rotation, &input, NULL) ==
		MH_ERROR_NONE);
	input.velocity[0] = input.previous_velocity[0] = 30;

	for (wheel = 0; wheel < 4; wheel++)
		input.extension[wheel] = .2;

	CHECK(mh_motion_step(car, col, &input, &forces, .03, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.passes[1].wheels[0].start[0] < 1);
	CHECK(result.passes[2].wheels[0].start[0] > 1);
	CHECK(result.passes[0].wheels[0].contact.polygon_tests > 0);
	CHECK(result.passes[1].wheels[0].contact.polygon_tests > 0);
	CHECK(result.passes[2].wheels[0].contact.polygon_tests == 0);
	CHECK(result.passes[2].wheels[0].contact.material == 7 &&
		result.passes[2].wheels[0].supported);
	CHECK(mh_motion_step(car, col, &result.state, &forces, .03, &next,
		NULL) == MH_ERROR_NONE);
	CHECK(next.passes[0].wheels[0].contact.material == 9);
	CHECK(next.passes[0].wheels[0].contact.polygon_tests > 0);

	/* Crossing on the second pass must refresh even with prior support. */
	input.position[0] = .9;
	CHECK(mh_motion_step(car, col, &input, &forces, .03, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.passes[0].wheels[0].supported);
	CHECK(result.passes[1].wheels[0].contact.material == 9);
	CHECK(result.passes[1].wheels[0].contact.hit &&
		!result.passes[1].wheels[0].supported);
	CHECK(result.passes[2].wheels[0].contact.polygon_tests > 0);
	CHECK(result.passes[2].wheels[0].contact.material == 9);
}

int
main(void)
{
	struct mh_collision col;
	struct mh_collision_cell cell;
	struct mh_collision_leaf leaf;
	struct mh_collision_polygon polygons[2];
	unsigned long indices[2];
	struct mh_car car;

	fixture(&col, &cell, &leaf, indices, polygons);
	memset(&car, 0, sizeof(car));
	car.has_suspension = 1;
	car.mass = 1000;
	car.spring_length = .5;
	test_contact(&col);
	test_suspension(&col, &car);
	test_endpoints();
	test_passes(&col, &car);

	return failures != 0;
}
