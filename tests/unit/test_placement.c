#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/route.h"
#include "engine/assets/scene.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/simulation/placement.h"
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

static const char square[] = "LWMO\n1\n9\n5\n"
			"0 0 0 0 0 0 1 1 1\n0 1 0 0 0\n"
			"0 0 10 0 0 0 1 1 1\n10 1 0 0 0\n"
			"10 0 10 0 0 0 1 1 1\n20 1 0 0 0\n"
			"10 0 0 0 0 0 1 1 1\n30 1 0 0 0\n"
			"0 0 0 0 0 0 1 1 1\n40 1 0 0 0\n";

static void
check_short_motion(void)
{
	static const unsigned char start[] =
		"LWMO\n1\n9\n1\n1 2 3 90 0 0 1 1 1\n0 0 0 0 0\n";
	struct mh_scene_object motion, saved;
	struct mh_route route, saved_route;
	double values[9];
	size_t n;

	CHECK(mh_motion_mot_parse(start, sizeof(start) - 1, &motion, NULL) ==
		MH_ERROR_NONE);
	CHECK(motion.key_count == 1 && motion.end_behavior == 2);
	CHECK(mh_scene_channels(&motion, 100, values, NULL) == MH_ERROR_NONE);
	CHECK(values[0] == 1 && values[1] == 2 && values[2] == 3 &&
		values[3] == 90);
	saved = motion;

	for (n = 0; n < sizeof(start) - 2; n++) {
		CHECK(mh_motion_mot_parse(start, n, &motion, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&motion, &saved, sizeof(motion)) == 0);
	}

	memset(&route, 0xa5, sizeof(route));
	saved_route = route;
	CHECK(mh_route_mot_parse(start, sizeof(start) - 1, &route, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&route, &saved_route, sizeof(route)) == 0);
	free(motion.keys);
}

static void
check_motion_decoding(void)
{
	static const unsigned char single[] =
		"LWMO\n1\n9\n1\n1 2 3 90 0 0 1 1 1\n0 0 0 0 0\n";
	const unsigned char *data;
	unsigned char *wrapped;
	struct mh_scene_object motion, saved_motion;
	struct mh_route route, saved_route;
	size_t size, wrapped_size;
	unsigned int sample, encoding;
	enum mh_error_code status;

	for (sample = 0; sample < 2; sample++) {
		data = sample ? single : (const unsigned char *)square;
		size = sample ? sizeof(single) - 1 : sizeof(square) - 1;
		status = mh_tbl_encode_named(
			data, size, NULL, &wrapped, &wrapped_size, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status != MH_ERROR_NONE)
			return;

		for (encoding = 0; encoding < 2; encoding++) {
			memset(&route, 0xa5, sizeof(route));
			saved_route = route;
			status = mh_motion_mot_decode(encoding ? wrapped : data,
				encoding ? wrapped_size : size, &motion, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status == MH_ERROR_NONE) {
				CHECK(motion.key_count == (sample ? 1 : 5));
				CHECK(motion.end_behavior == 2);
				CHECK(motion.keys[motion.key_count - 1].time ==
					(sample ? 0 : 40));
				free(motion.keys);
			}

			status = mh_route_mot_decode(encoding ? wrapped : data,
				encoding ? wrapped_size : size, &route, NULL);
			CHECK((status == MH_ERROR_NONE) == !sample);

			if (status == MH_ERROR_NONE)
				mh_route_free(&route);
			else
				CHECK(memcmp(&route, &saved_route,
					sizeof(route)) == 0);
		}

		memset(&motion, 0xa5, sizeof(motion));
		saved_motion = motion;
		route = saved_route;
		wrapped[3] = 7;
		CHECK(mh_motion_mot_decode(wrapped, wrapped_size, &motion,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&motion, &saved_motion, sizeof(motion)) == 0);
		CHECK(mh_route_mot_decode(wrapped, wrapped_size, &route,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&route, &saved_route, sizeof(route)) == 0);
		free(wrapped);
	}
}

static void
check_vehicle_placement(const struct mh_placement grid[4])
{
	struct mh_car car;
	struct mh_engine_config config;
	struct mh_engine_options options;
	struct mh_vehicle_state state, relocated, held;
	double world[3], velocity[3];
	unsigned int i;

	memset(&car, 0, sizeof(car));
	memset(&options, 0, sizeof(options));
	car.has_engine = car.has_grip = 1;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.brake_force = .5;
	car.gears = 1;
	car.gear_ratio[0] = 50;
	car.gear_ratio[1] = 70;
	car.acceleration_force = 2800;
	car.acceleration_level = car.speed_level = 5;
	CHECK(mh_engine_configure(&car, &options, &config, NULL) ==
		MH_ERROR_NONE);
	memset(&state, 0x5a, sizeof(state));
	CHECK(mh_vehicle_place(&config, grid, &state, NULL) == MH_ERROR_NONE);
	CHECK(mh_motion_display(&state.motion, world, velocity, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		NEAR(world[i], grid[0].position[i]);
		NEAR(state.motion.velocity[i], 0);
		NEAR(state.motion.previous_angular_velocity[i], 0);
	}

	CHECK(state.engine.gear == 1 && state.engine.rpm == 1000);
	CHECK(state.grip.rear_limit == 0 && state.body.contact == 0 &&
		state.smoothed_steering == 0);

	for (i = 0; i < 4; i++)
		NEAR(state.motion.extension[i], 1);

	state.engine.detached = 1;
	state.smoothed_steering = .75;
	state.grip.wheels[2].supported = 1;
	state.grip.wheels[2].normal_y = .5;
	CHECK(mh_vehicle_relocate(&config, grid + 1, &state, &relocated,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_vehicle_relocate(&config, grid + 1, &state, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &relocated, sizeof(state)) == 0);
	held = state;
	config.gears = 0;
	CHECK(mh_vehicle_relocate(&config, grid, &state, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&state, &held, sizeof(state)) == 0);
}

int
main(void)
{
	struct mh_route route, saved_route;
	struct mh_track track;
	struct mh_placement grid[4], result, saved;
	char data[1024];
	double position[3], diagonal;
	unsigned int i;
	size_t n;

	check_short_motion();
	check_motion_decoding();
	CHECK(mh_route_mot_parse((const unsigned char *)square, strlen(square),
		&route, NULL) == MH_ERROR_NONE);
	CHECK(route.motion.key_count == 5 && route.motion.keys[4].time == 40);
	saved_route = route;

	for (n = 0; n < strlen(square) - 1; n++) {
		CHECK(mh_route_mot_parse((const unsigned char *)square, n,
			&route, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&route, &saved_route, sizeof(route)) == 0);
	}

	strcpy(data, square);
	strcat(data, "EndBehavior 2\n");
	CHECK(mh_route_mot_parse((const unsigned char *)data, strlen(data),
		&route, NULL) != MH_ERROR_NONE);
	strcpy(data, square);
	memcpy(strstr(data, "20 1"), "10", 2);
	CHECK(mh_route_mot_parse((const unsigned char *)data, strlen(data),
		&route, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&route, &saved_route, sizeof(route)) == 0);
	memset(&track, 0, sizeof(track));
	track.has_start_grid = 1;
	track.start_grid[0] = 12;
	track.start_grid[1] = 6;
	track.start_grid[2] = 3;
	track.start_grid[3] = 5.5;
	diagonal = 1 / sqrt(2.0);

	for (i = 0; i < 4; i++)
		CHECK(mh_placement_grid(&track, &route, i, grid + i, NULL) ==
			MH_ERROR_NONE);

	NEAR(grid[0].position[0], 3 * diagonal);
	NEAR(grid[0].position[2], -9 * diagonal);
	NEAR(grid[0].rotation[6], -diagonal);
	NEAR(grid[0].rotation[8], diagonal);
	NEAR(grid[1].position[0], 14.5 * diagonal);
	NEAR(grid[1].position[2], -8.5 * diagonal);
	NEAR(grid[2].position[0] - grid[0].position[0], 12 * diagonal);
	NEAR(grid[3].position[2] - grid[1].position[2], -12 * diagonal);
	position[0] = -2;
	position[1] = 1;
	position[2] = 5;
	CHECK(mh_placement_recover(&route, position, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.route_parameter, 5);
	NEAR(result.position[0], 0);
	NEAR(result.position[2], 5);
	NEAR(result.rotation[0], 1);
	NEAR(result.rotation[4], 1);
	NEAR(result.rotation[8], 1);

	/* Original omits the closing key during relocation's linear sample. */
	position[0] = 5;
	position[2] = -2;
	CHECK(mh_placement_recover(&route, position, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.route_parameter, 35);
	NEAR(result.position[0], 10);
	NEAR(result.position[2], 0);
	saved = result;
	CHECK(mh_placement_grid(&track, &route, 64, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	position[0] = 1e7;
	CHECK(mh_placement_recover(&route, position, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	check_vehicle_placement(grid);

	/*
	 * Degenerate direction returns an error instead of asserting/looping.
	 */
	for (n = 0; n < route.motion.key_count; n++)
		memset(route.motion.keys[n].value, 0, 3 * sizeof(double));

	CHECK(mh_placement_grid(&track, &route, 0, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	mh_route_free(&route);
	CHECK(route.motion.keys == NULL && route.motion.key_count == 0);

	if (failures != 0)
		return 1;

	puts("LWMO validation, grid spacing, route recovery, origin conversion and reset passed");

	return 0;
}
