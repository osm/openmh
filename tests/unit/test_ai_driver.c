#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ai_path.h"
#include "engine/core/error.h"
#include "engine/simulation/ai_driver.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
make_path(struct mh_ai_path *path, struct mh_ai_point points[128],
	struct mh_ai_section sections[30])
{
	size_t i;

	memset(points, 0, 128 * sizeof(*points));
	memset(sections, 0, 30 * sizeof(*sections));

	for (i = 0; i < 128; i++) {
		points[i].z = (double)i;
		points[i].width[0] = points[i].width[1] = 5;
		points[i].direction[2] = 1;
		points[i].section = i < 64 ? 0 : 1;
		points[i].radius = i < 64 ? 0 : 20;
	}

	sections[0].count = sections[1].count = 64;
	sections[1].radius = 20;
	path->points = points;
	path->sections = sections;
	path->count = 128;
	path->section_count = 2;
}

static void
check_lookup(void)
{
	struct mh_ai_path path;
	struct mh_ai_point points[128];
	struct mh_ai_section sections[30];
	struct mh_ai_cursor cursor, old;
	struct mh_ai_location full;

	make_path(&path, points, sections);
	CHECK(mh_ai_follow(&path, 0, 0, 127, 127, &cursor, NULL) ==
		MH_ERROR_NONE);
	CHECK(cursor.point == 0 && cursor.corner == 0 && cursor.distance == 0);
	CHECK(mh_ai_follow(&path, 0, 127, 0, 64, &cursor, NULL) ==
		MH_ERROR_NONE);
	CHECK(cursor.point == 127 &&
		cursor.corner == 127); /* target follows after being passed */
	CHECK(mh_ai_follow(&path, 0, 10, 20, 64, &cursor, NULL) ==
		MH_ERROR_NONE);
	CHECK(cursor.point == 10 && cursor.corner == 64);
	CHECK(mh_ai_follow(&path, 0, 10.5, 10, 10, &cursor, NULL) ==
		MH_ERROR_NONE);
	CHECK(cursor.point == 10 && cursor.corner == 10 &&
		cursor.distance == .5);
	old = cursor;
	CHECK(mh_ai_follow(&path, 0, 0, 128, 0, &cursor, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_ai_follow(&path, 0, 0, 0, 128, &cursor, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_ai_follow(&path, 0, 1e7, 0, 0, &cursor, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&old, &cursor, sizeof(cursor)) == 0);

	/*
	 * A local minimum differs from the full search; do not jump branches.
	 */
	path.count = 4;
	points[0].z = 0;
	points[1].z = 10;
	points[2].z = 1;
	points[3].z = 20;
	CHECK(mh_ai_follow(&path, 0, 1, 0, 0, &cursor, NULL) == MH_ERROR_NONE &&
		cursor.point == 0);
	CHECK(mh_ai_locate(&path, 0, 1, &full, NULL) == MH_ERROR_NONE &&
		full.point == 2);

	/*
	 * Both neighbors improve: following gets priority over the nearer
	 * prior.
	 */
	points[0].z = 10;
	points[1].z = 5;
	points[2].z = 20;
	points[3].z = 0;
	CHECK(mh_ai_follow(&path, 0, 0, 0, 0, &cursor, NULL) == MH_ERROR_NONE &&
		cursor.point == 1);
}

static void
check_patched_follow(void)
{
	struct mh_ai_path path;
	struct mh_ai_point points[128];
	struct mh_ai_section sections[30];
	struct mh_ai_cursor cursor;

	make_path(&path, points, sections);
	path.count = 4;
	points[2].z = 200;
	points[3].z = 0;

	/* RedRock midpoint that cycles between points 0/1 before patch 2.1. */
	points[0].x = -0.3517122268676758;
	points[0].z = 41.0316162109375;
	points[1].x = -0.32021665573120117;
	points[1].z = 42.122650146484375;
	CHECK(mh_ai_follow(&path, -0.3359644412994385, 41.57713317871094, 0, 1,
		&cursor, NULL) == MH_ERROR_NONE);
	CHECK(cursor.point == 0 && cursor.corner == 1);

	/* RedRock points 54/55: both walks change the corner, then return. */
	points[0].x = 0.07870912551879883;
	points[0].z = 95.5735855102539;
	points[1].x = 0.0717167854309082;
	points[1].z = 96.48748016357422;
	points[3].x = 0.08737325668334961;
	points[3].z = 94.48235321044922;
	CHECK(mh_ai_follow(&path, 0.07521295547485352, 96.03053283691406, 0, 1,
		&cursor, NULL) == MH_ERROR_NONE);
	CHECK(cursor.point == 0 && cursor.corner == 0);
}

static void
check_controls(void)
{
	struct mh_ai_path path;
	struct mh_ai_point points[128];
	struct mh_ai_section sections[30];
	struct mh_ai_driver_config config;
	struct mh_ai_driver_state state;
	struct mh_ai_driver_feedback feedback;
	struct mh_ai_driver_result result, saved;
	double target, positive;
	size_t i;

	make_path(&path, points, sections);
	config.mass = 1000;
	config.corner_grip = 2;
	config.corner_steering = 14;
	memset(&feedback, 0, sizeof(feedback));
	feedback.heading_z = 1;
	CHECK(mh_ai_driver_start(&path, 0, 0, &state, NULL) == MH_ERROR_NONE);
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 1 && result.brake == 0 &&
		result.steering == 0 && result.scan_points == 0);
	CHECK(result.point_target_speed == 10 &&
		result.state.throttle_limit == 1);

	/* Scan follows pedal decision, introducing a one-update delay. */
	feedback.z = 10;
	feedback.speed = 70;
	CHECK(mh_ai_driver_start(&path, 0, 10, &state, NULL) == MH_ERROR_NONE);
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 1 && result.brake == 0 &&
		result.state.corner == 64);
	CHECK(result.scan_points == 87 && result.state.brake_points == 80);
	state = result.state;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 0 && result.brake == (double)(float).89);

	/* Equal braking-start positions select the later scanned candidate. */
	points[65].radius = 20 - 56 / (9.82 * 2);
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.corner == 65 && result.state.brake_points == 81);
	points[65].radius = 20;

	/* A slower tick deliberately keeps the previous braking distance. */
	feedback.speed = 10;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 1 && result.state.brake_points == 80);

	/*
	 * A wrapped target is compared using raw indices, not forward distance.
	 */
	feedback.z = 120;
	feedback.speed = 40;
	points[2].radius = 20;
	CHECK(mh_ai_driver_start(&path, 0, 120, &state, NULL) == MH_ERROR_NONE);
	state.corner = 2;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.brake == (double)(float).89 && result.state.corner == 2 &&
		result.scan_points == 28);
	points[2].radius = 0;
	feedback.z = 64;
	target = sqrt(20 * 9.82 * 2);
	CHECK(mh_ai_driver_start(&path, 0, 64, &state, NULL) == MH_ERROR_NONE);
	feedback.speed = target * (double)(float).95;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == .5 && result.brake == 0 &&
		result.feedforward > 0);
	feedback.speed = target;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.throttle == 0 && result.brake == 0);
	feedback.speed = target * (double)(float)1.52;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.brake == 0);
	feedback.speed += .01;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.brake > .88);
	feedback.boosted = 1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(fabs(result.point_target_speed - target * 1.25) < 1e-12 &&
		result.brake == 0);
	feedback.boosted = 0;
	feedback.speed = 10;
	feedback.wheelspin = 1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == 0 && result.state.throttle_limit == .5);
	state = result.state;
	feedback.wheelspin = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.throttle == .75 && result.state.throttle_limit == .75);
	positive = result.feedforward;
	sections[1].radius = -20;

	for (i = 64; i < 128; i++)
		points[i].radius = -20;

	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.feedforward == -positive);

	/* Signed lateral distance, lane decay, damping and heading override. */
	feedback.z = 10;
	feedback.x = -2;
	feedback.speed = 20;
	CHECK(mh_ai_driver_start(&path, -2, 10, &state, NULL) ==
			MH_ERROR_NONE &&
		state.lane == 2);
	state.lane = 0;
	state.previous_lateral = -1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.lateral == -2 && result.lateral_rate == -2 &&
		result.correction == .1);
	CHECK(fabs(result.damping + (double)(float)3.9 * .1) < 1e-12 &&
		result.steering > 0);
	state.lane = 2;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.lane == 2 - 2 * (double)(float).2);
	feedback.heading_x = 1;
	feedback.heading_z = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.steering == -1);
	feedback.heading_x = 0;
	feedback.heading_z = 1;
	feedback.x = 6;
	feedback.speed = 10;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping == 0);
	feedback.x = -6;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping != 0);
	feedback.speed = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping == 0);

	/*
	 * Contact projection cancellation must not divide by a near-zero speed.
	 */
	feedback.speed = 5.5511151231257827e-16;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping == 0);
	feedback.speed = -5.5511151231257827e-16;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping == 0);
	feedback.speed = 1e-8;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .5, &result,
		NULL) == MH_ERROR_NONE &&
		result.damping != 0);

	/* Original section-29 exception with a count guard. */
	path.section_count = 30;
	sections[29].radius = 150.5;
	points[10].section = 29;
	feedback.x = 0;
	feedback.speed = 51;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.brake > .88);

	/* Bad/unsupported inputs preserve the entire caller output. */
	saved = result;
	feedback.heading_z = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	feedback.heading_z = 1;
	feedback.speed = 1000;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	feedback.speed = -1001;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	feedback.speed = 10;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 0, &result,
		NULL) != MH_ERROR_NONE);
	config.mass = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	config.mass = 1000;
	state.corner = 128;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	state.corner = 10;
	points[10].width[0] = points[10].width[1] = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	points[10].width[0] = points[10].width[1] = 5;
	points[10].radius = 1;
	sections[29].radius = 1 / .0015;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	feedback.x = HUGE_VAL;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
}

static void
check_recovery(void)
{
	struct mh_ai_path path;
	struct mh_ai_point points[128];
	struct mh_ai_section sections[30];
	struct mh_ai_driver_config config;
	struct mh_ai_driver_state state;
	struct mh_ai_driver_feedback feedback;
	struct mh_ai_driver_result result;
	unsigned int i;

	make_path(&path, points, sections);
	config.mass = 1000;
	config.corner_grip = 2;
	config.corner_steering = 14;
	memset(&feedback, 0, sizeof(feedback));
	feedback.x = 3;
	feedback.z = 10;
	feedback.heading_z = 1;
	CHECK(mh_ai_driver_start(&path, feedback.x, feedback.z, &state, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 1,
			&result, NULL) == MH_ERROR_NONE);
		CHECK(result.throttle == 1 &&
			result.state.reversing == (i == 2));
		state = result.state;
	}

	CHECK(state.stuck_seconds == 2);

	for (i = 0; i < 3; i++) {
		CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, 1,
			&result, NULL) == MH_ERROR_NONE);
		CHECK(result.brake == .5 && result.state.reversing == (i < 2));
		state = result.state;
	}

	/*
	 * Centered and aligned recovery cancels the flag, after this tick's
	 * pedals.
	 */
	state.reversing = 1;
	state.stuck_seconds = 2;
	feedback.x = 1;
	feedback.speed = -2;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.brake == .5 && !result.state.reversing &&
		result.state.stuck_seconds == 0);

	/*
	 * Wrong-direction latch is chosen once, retained, then cleared forward.
	 */
	CHECK(mh_ai_driver_start(&path, -3, 10, &state, NULL) == MH_ERROR_NONE);
	feedback.x = -3;
	feedback.speed = 2;
	feedback.heading_z = -1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.steering == -1 && result.state.turn_latch == -1 &&
		result.state.previous_lateral == 0);
	state = result.state;
	feedback.x = 3;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.steering == -1);
	state.turn_latch = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.steering == 1);
	feedback.x = 0;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.steering == -1);
	feedback.heading_z = 1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.state.turn_latch == 0);

	/*
	 * Off-road entry, throttle override, perpendicular stop and exit
	 * ordering.
	 */
	CHECK(mh_ai_driver_start(&path, 11, 10, &state, NULL) == MH_ERROR_NONE);
	state.throttle_limit = .5;
	feedback.x = 11;
	feedback.speed = 14;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		!result.state.offroad);
	feedback.speed = 10;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(result.state.offroad && result.steering == -1 &&
		result.throttle == 1);
	state = result.state;
	feedback.heading_z = 0;
	feedback.heading_x = 1;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE &&
		result.steering == 0);
	feedback.heading_z = 1;
	feedback.heading_x = 0;
	feedback.x = 4;
	CHECK(mh_ai_driver_step(&path, &config, &state, &feedback, .1, &result,
		NULL) == MH_ERROR_NONE);
	CHECK(!result.state.offroad && result.steering == -1);
}

int
main(void)
{

	check_lookup();
	check_patched_follow();
	check_controls();
	check_recovery();

	if (failures != 0)
		return 1;

	puts("AI path walk, delayed braking, pedals, steering and atomic failures passed");

	return 0;
}
