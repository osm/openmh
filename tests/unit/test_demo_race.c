#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/assets/material.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/demo/ghost_capture.h"
#include "engine/demo/race.h"
#include "engine/game/cheats.h"
#include "engine/simulation/tick_clock.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_demo_race race, saved_race, initial_race;
static struct mh_drive cars[MH_RACE_MAX_CARS];
static struct mh_car car;
static struct mh_collision col, hulls[MH_RACE_MAX_CARS];
static struct mh_materials materials;
static struct mh_track track;
static struct mh_route route;
static struct mh_scene_key keys[65];
static unsigned char data[MH_DEMO_MAX_HEADER_BYTES + 129 * 398], empty[62],
	hull_bytes[278];

static int
fixture_version(unsigned int count, unsigned int local, int patched,
	struct mh_demo *demo)
{
	static const unsigned char defs[] = "Materials 1\nMaterialName road\n";
	static const unsigned char surfaces[] = "Materials 1\nMaterial road\n";
	struct mh_net_vehicle vehicle;
	unsigned int i, j;
	size_t stride, header_bytes;
	unsigned int player_bytes;
	unsigned char *frame_bytes;

	memset(empty, 0, sizeof(empty));
	empty[0] = 42;
	empty[4] = empty[6] = empty[8] = empty[12] = empty[14] = 1;
	empty[34] = empty[38] = 0x80;
	empty[35] = empty[39] = 0x3f;
	mh_collision_free(&col);
	mh_collision_free(&hulls[0]);
	CHECK(mh_collision_col_decode(empty, sizeof(empty), &col, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_collision_col_decode(hull_bytes, pair_fixture(hull_bytes),
		&hulls[0], NULL) == MH_ERROR_NONE);

	for (i = 1; i < MH_RACE_MAX_CARS; i++)
		hulls[i] = hulls[0];

	CHECK(mh_materials_mat_srf_decode(defs, sizeof(defs) - 1, surfaces,
		sizeof(surfaces) - 1, &materials, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	strcpy(car.name, "Kobra");
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

	for (i = 0; i < 4; i++) {
		car.corners[i][0] = i == 0 || i == 3 ? -1 : 1;
		car.corners[i][2] = i < 2 ? 2 : -2;
	}

	memset(&track, 0, sizeof(track));
	track.has_start_grid = 1;
	track.laps = 1;
	track.view_distance = 180;
	strcpy(track.name, "RedRock");
	track.start_grid[0] = 12;
	track.start_grid[1] = 6;
	track.start_grid[2] = 3;
	track.start_grid[3] = 5.5;
	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 65;
	route.motion.end_behavior = 2;

	for (i = 0; i < 65; i++) {
		keys[i].time = i * 10;
		keys[i].linear = 1;
		keys[i].value[0] = 40 * cos(i * 6.283185307179586 / 64);
		keys[i].value[1] = .62;
		keys[i].value[2] = 40 * sin(i * 6.283185307179586 / 64);
	}

	keys[64].value[0] = keys[0].value[0];
	keys[64].value[2] = keys[0].value[2];

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0,
			0, 1, &cars[i], NULL) == MH_ERROR_NONE);

	header_bytes = patched == 2 ? 6713 : patched ? 6705 : 4012;
	player_bytes = patched ? 81 : 65;
	memset(data, 0, sizeof(data));
	pair_put32(data,
		patched == 2	  ? 0x40800000UL
			: patched ? 0x40400000UL
				: 0x40200000UL);

	if (patched == 2) {
		pair_put32(data + 1089, 1);
		pair_put32(data + 1093, 120);
	}

	pair_put32(data + 24, 1);
	pair_put32(data + 28, count);
	pair_put32(data + 32, local);
	stride = 8 + 30 * count;

	for (i = 0; i < count; i++)
		pair_put32(data + 36 + player_bytes * i, 1);

	memset(&vehicle, 0, sizeof(vehicle));
	vehicle.gear = 1;
	vehicle.rpm = 2000;
	vehicle.flags = 1026;
	vehicle.position[1] = .62;

	for (i = 0; i < 129; i++) {
		frame_bytes = data + header_bytes + i * stride;
		pair_put32(frame_bytes, (1U << count) - 1);
		pair_put32(frame_bytes + 4, 100 + 100 * i);

		for (j = 0; j < count; j++) {
			vehicle.position[0] = 40 + 5 * j + .05 * i;
			vehicle.wheel_speed[0] = 2 + .25 * i;
			vehicle.wheel_speed[1] = 4 + .5 * i;
			CHECK(mh_net_vehicle_encode(&vehicle,
				frame_bytes + 8 + 30 * j, 30,
				NULL) == MH_ERROR_NONE);
		}
	}

	CHECK(mh_demo_read(data, header_bytes + 129 * stride, demo, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static int
fixture(unsigned int count, unsigned int local, struct mh_demo *demo)
{
	return fixture_version(count, local, 0, demo);
}

static int
patched_fields(void)
{
	struct mh_demo demo, header;
	struct mh_demo_layout layout;
	struct mh_error error;
	enum mh_error_code status;
	struct mh_profile human;
	struct mh_engine_input controls;
	unsigned int count, ghost, expected, entry, slot, frame, steps;

	memset(&controls, 0, sizeof(controls));
	memset(&human, 0, sizeof(human));
	strcpy(human.nick, "Human");

	for (count = 1; count <= 13; count++) {
		for (ghost = 0; ghost < 2; ghost++) {
			CHECK(fixture_version(count, count - 1, 1, &demo) == 0);
			expected = count + ghost;

			if (expected > 13)
				expected = 13;

			CHECK(mh_demo_race_layout(&demo, (int)ghost, &layout,
				NULL) == MH_ERROR_NONE);
			CHECK(layout.count == expected);
			CHECK(layout.recording[0] ==
				(ghost ? MH_DEMO_HUMAN_SLOT : count - 1));
			CHECK(mh_demo_race_begin(&demo, (int)ghost, cars,
				expected, hulls, &race,
				NULL) == MH_ERROR_NONE);
			CHECK(race.field.count == expected &&
				race.field.active_mask == (1U << expected) - 1);
			CHECK(race.field.standings.count == expected);

			for (entry = ghost; entry < expected; entry++) {
				slot = layout.recording[entry];
				CHECK(slot < 13);
				CHECK(race.field.cars[entry]
						.vehicle.motion.position[0] ==
					40 + 5 * slot);
				CHECK(race.field.cars[entry].remote);
				CHECK(race.field.hulls[entry] == &hulls[entry]);
			}

			CHECK(mh_field_select_view(&race.field, expected - 1,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_field_camera_mode(&race.field, MH_CAMERA_CHASE,
				NULL) == MH_ERROR_NONE);
			status = mh_demo_race_frame(
				&race, &controls, 6.0005, 0, &steps, &error);

			if (status != MH_ERROR_NONE)
				fprintf(stderr,
					"patched field %u Ghost %u: %s\n",
					count, ghost, error.message);

			CHECK(status == MH_ERROR_NONE);

			for (frame = 0; frame < 80; frame++)
				CHECK(mh_demo_race_frame(&race, &controls, .25,
					0, &steps,
					NULL) == MH_ERROR_NONE);

			CHECK(!race.playback.active &&
				race.playback.next_frame == 129);
			slot = race.layout.recording[expected - 1];
			CHECK(race.field.cars[expected - 1]
					.vehicle.motion.position[0] >
				40 + 5 * slot);
			CHECK(race.field.viewed_entry == expected - 1);
			CHECK(race.field.active_mask == (1U << expected) - 1);
			CHECK(race.field.standings.count == expected);

			if (ghost) {
				CHECK(race.field.unavailable_mask ==
					((1U << expected) - 2));

				for (entry = 0; entry < expected; entry++)
					CHECK(race.field.contact_count[entry] ==
						0);
			}

			CHECK(mh_demo_race_restart(&race, NULL) ==
				MH_ERROR_NONE);
			CHECK(race.playback.active &&
				race.field.viewed_entry == 0);

			if (ghost)
				CHECK(mh_ghost_capture_header(&race, &human,
					&header, NULL) == MH_ERROR_NONE);
		}
	}

	/*
	 * Patched high-slot flags remap to field entries, including the viewer.
	 */
	CHECK(fixture_version(13, 12, 1, &demo) == 0);

	for (frame = 0; frame < 129; frame++)
		pair_put32(data + 6705 + 398 * frame,
			(frame == 0 ? 8191UL : 8191UL & ~(1UL << 11)) |
				(1UL << 25));

	CHECK(mh_demo_race_begin(&demo, 0, cars, 13, hulls, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 1);
	CHECK(mh_field_select_view(&race.field, 12, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_race_frame(&race, &controls, 6.0005, 0, &steps, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.field.active_mask == 4095 && race.field.viewed_entry == 0);
	CHECK(race.field.unavailable_mask == 1);
	CHECK(race.field.standings.cars[12].position == 0);
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);
	CHECK(race.field.active_mask == 8191 &&
		race.field.unavailable_mask == 1);

	/*
	 * Increasing playback storage must not expand the live/AI admission
	 * rules.
	 */
	CHECK(mh_field_start_network(cars, 9, &saved_race.field, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_field_start(&cars[0], NULL, 8, NULL, 0, &saved_race.field,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_field_start_playback(cars, MH_RACE_MAX_CARS + 1, 0,
		&saved_race.field, NULL) != MH_ERROR_NONE);

	/* Sparse slot twelve is data, not a live-human sentinel. */
	CHECK(fixture_version(2, 0, 1, &demo) == 0);
	pair_put32(data + 36 + 81, 0);
	pair_put32(data + 36 + 81 * 12, 1);
	pair_put32(data + 32, 12);
	CHECK(mh_demo_read(data, 6705 + 129 * 68, &demo, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_race_begin(&demo, 0, cars, 2, hulls, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.layout.recording[0] == 12 && race.layout.recording[1] == 0);
	CHECK(race.field.cars[0].vehicle.motion.position[0] == 45);
	CHECK(mh_demo_race_begin(&demo, 1, cars, 3, hulls, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.layout.recording[0] == MH_DEMO_HUMAN_SLOT &&
		race.layout.recording[2] == 12);
	CHECK(race.field.cars[2].vehicle.motion.position[0] == 45);

	return 0;
}

static int
capture_order(void)
{
	static struct mh_field local, saved_field;
	struct mh_field *field;
	struct mh_demo demo, header;
	struct mh_profile human;
	struct mh_demo_capture_frame frame, saved_frame;
	struct mh_demo_record_sample recovered_sample;
	struct mh_engine_input controls;
	struct mh_net_vehicle vehicle;
	double position[3] = {40, -201, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned long random, saved_random;
	unsigned int mode, steps;
	enum mh_error_code status;

	memset(&human, 0, sizeof(human));
	strcpy(human.nick, "Capture");
	memset(&controls, 0, sizeof(controls));

	for (mode = 0; mode < 2; mode++) {
		CHECK(fixture(1, 0, &demo) == 0);

		if (mode) {
			CHECK(mh_demo_race_begin(&demo, 1, cars, 2, hulls,
				&race, NULL) == MH_ERROR_NONE);
			CHECK(mh_ghost_capture_header(&race, &human, &header,
				NULL) == MH_ERROR_NONE);
			field = &race.field;
		} else {
			CHECK(mh_field_start(&cars[0], NULL, 0, NULL, 0, &local,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_demo_capture_header(&local, &human, &header,
				NULL) == MH_ERROR_NONE);
			field = &local;
		}

		random = 123;
		status = mode
			? mh_ghost_capture_advance(&race, &controls, 6.1, 0,
				&header, &frame, &steps, &random, NULL)
			: mh_demo_capture_advance(&local, &controls, 6.1, 0,
				&header, &frame, &steps, &random, NULL);

		CHECK(status == MH_ERROR_NONE && frame.ready && steps == 10);
		saved_field = *field;
		saved_race = race;
		status = mode
			? mh_ghost_capture_advance(&race, &controls, 30, 1,
				&header, &frame, &steps, &random, NULL)
			: mh_demo_capture_advance(&local, &controls, 30, 1,
				&header, &frame, &steps, &random, NULL);

		CHECK(status == MH_ERROR_NONE && !frame.ready && steps == 0 &&
			memcmp(&saved_field, field, sizeof(saved_field)) == 0);

		if (mode)
			CHECK(memcmp(&saved_race, &race, sizeof(race)) == 0);

		/*
		 * Physics keeps this fallen pose until frame-end recovery. A
		 * wheel error occurs AFTER capture and must discard both bytes
		 * and world.
		 */
		CHECK(mh_motion_reset(position, rotation,
			&field->cars[0].vehicle.motion,
			NULL) == MH_ERROR_NONE);
		field->cars[0].wheels.angle[0] = 1e7;
		saved_field = *field;
		saved_race = race;
		saved_frame = frame;
		saved_random = random;
		steps = 99;
		status = mode
			? mh_ghost_capture_advance(&race, &controls, .25, 0,
				&header, &frame, &steps, &random, NULL)
			: mh_demo_capture_advance(&local, &controls, .25, 0,
				&header, &frame, &steps, &random, NULL);

		CHECK(status != MH_ERROR_NONE && steps == 99 &&
			random == saved_random);
		CHECK(memcmp(&saved_field, field, sizeof(saved_field)) == 0 &&
			memcmp(&frame, &saved_frame, sizeof(frame)) == 0);

		if (mode)
			CHECK(memcmp(&saved_race, &race, sizeof(race)) == 0);

		field->cars[0].wheels.angle[0] = 0;
		status = mode
			? mh_ghost_capture_advance(&race, &controls, .25, 0,
				&header, &frame, &steps, &random, NULL)
			: mh_demo_capture_advance(&local, &controls, .25, 0,
				&header, &frame, &steps, &random, NULL);

		CHECK(status == MH_ERROR_NONE && frame.ready && steps == 7);
		CHECK(field->cars[0].recovery_event == MH_RECOVERY_FALL);
		CHECK(mh_net_vehicle_decode(frame.sample.bodies[0], 30,
			&vehicle, NULL) == MH_ERROR_NONE &&
			vehicle.position[1] < -200);
		status = mode ? mh_ghost_capture_sample(
					&race, &header, &recovered_sample, NULL)
			: mh_demo_capture_sample(&local, &header,
					&recovered_sample, NULL);

		CHECK(status == MH_ERROR_NONE);
		CHECK(mh_net_vehicle_decode(recovered_sample.bodies[0], 30,
			&vehicle, NULL) == MH_ERROR_NONE &&
			vehicle.position[1] > -200);

		if (mode) {
			CHECK(mh_net_vehicle_decode(frame.sample.bodies[1], 30,
				&vehicle, NULL) == MH_ERROR_NONE &&
				vehicle.position[0] > 40);
			CHECK(memcmp(frame.sample.bodies[1],
				recovered_sample.bodies[1], 30) == 0);
		}
	}

	puts("capture order: pre-recovery human pose, newly applied Ghost pose, pause and late-failure rollback passed");

	return 0;
}

static int
native_playback(void)
{
	struct mh_demo demo;
	struct mh_engine_input controls;
	unsigned int steps, ghost, schedule, i, car, axis, due;
	const unsigned int fps[] = {120, 30, 60, 144, 200, 260, 360};
	static double reference[2400][16];
	double sample[16], *value;
	struct mh_tick_clock clock;

	memset(&controls, 0, sizeof(controls));

	for (ghost = 0; ghost < 2; ghost++)
		for (schedule = 0; schedule < sizeof(fps) / sizeof(fps[0]);
			schedule++) {
			CHECK(fixture_version(1, 0, 2, &demo) == 0);
			CHECK(mh_demo_race_begin(&demo, (int)ghost, cars,
				1 + ghost, hulls, &race,
				NULL) == MH_ERROR_NONE);
			CHECK(race.field.cars[0].frame.fixed_rate == 120);
			race.playback.benchmark = !ghost;

			mh_tick_clock_init(&clock, 120, NULL);

			while (clock.ticks < 2400) {
				CHECK(mh_tick_clock_add(&clock,
					1.0L / fps[schedule], 0,
					NULL) == MH_ERROR_NONE);
				due = mh_tick_clock_due(&clock);

				for (i = 0; i < due && clock.ticks < 2400;
					i++) {
					CHECK(mh_demo_race_frame(&race,
						&controls, 1.0L / 120,
						MH_FRAME_RUN, &steps,
						NULL) == MH_ERROR_NONE);
					memset(sample, 0, sizeof(sample));
					value = sample;

					for (car = 0; car < race.field.count;
						car++)
						for (axis = 0; axis < 3;
							axis++) {
							*value++ =
								race.field
									.cars[car]
									.vehicle
									.motion
									.position
										[axis];
							*value++ =
								race.field
									.cars[car]
									.vehicle
									.motion
									.velocity
										[axis];
						}

					*value++ = race.playback.next_frame;
					*value++ =
						race.field.cars[0].race.now_ms;
					*value++ = race.field.cars[0]
							.vehicle.engine.gear;
					*value++ = race.field.cars[0]
							.vehicle.engine.rpm;

					if (schedule == 0)
						memcpy(reference[clock.ticks],
							sample, sizeof(sample));
					else
						CHECK(memcmp(reference[clock.ticks],
							sample,
							sizeof(sample)) ==
							0);
					mh_tick_clock_commit(&clock, NULL);
				}
			}

			CHECK(!race.playback.active &&
				race.playback.next_frame == 129);
			CHECK(ghost || race.benchmark.complete);
			CHECK(mh_demo_race_restart(&race, NULL) ==
				MH_ERROR_NONE);
			CHECK(race.field.cars[0].frame.fixed_rate == 120 &&
				race.field.cars[0].frame.fixed_ticks == 0);
			CHECK(mh_demo_race_frame(&race, &controls, 1.0L / 120,
				MH_FRAME_RUN, &steps,
				NULL) == MH_ERROR_NONE);
			CHECK(race.field.cars[0].frame.fixed_ticks == 1);
		}

	return 0;
}

int
main(void)
{
	struct mh_demo demo;
	struct mh_demo_layout layout;
	struct mh_engine_input controls;
	struct mh_records records;
	struct mh_demo_playback finished_playback;
	struct mh_wheel_state wheels;
	unsigned int count, local, i, steps;
	double position[3], velocity[3],
		rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	unsigned long score;

	CHECK(native_playback() == 0);
	CHECK(capture_order() == 0);
	CHECK(patched_fields() == 0);

	/*
	 * Camera/local entry mapping for every supported roster and local slot.
	 */
	memset(&demo, 0, sizeof(demo));

	for (count = 1; count <= 8; count++)
		for (local = 0; local < count; local++) {
			demo.player_count = count;
			demo.local_player = local;

			for (i = 0; i < 8; i++)
				demo.players[i].active = i < count;

			CHECK(mh_demo_race_layout(&demo, 0, &layout, NULL) ==
					MH_ERROR_NONE &&
				layout.count == count);
			CHECK(layout.recording[0] == local);

			for (i = 1; i < count; i++)
				CHECK(layout.recording[i] ==
					(i <= local ? i - 1 : i));

			CHECK(mh_demo_race_layout(&demo, 1, &layout, NULL) ==
					MH_ERROR_NONE &&
				layout.count == (count < 8 ? count + 1 : 8));
			CHECK(layout.recording[0] == MH_DEMO_HUMAN_SLOT);

			for (i = 1; i < layout.count; i++)
				CHECK(layout.recording[i] == i - 1);
		}

	CHECK(fixture(3, 2, &demo) == 0);
	CHECK(mh_demo_race_begin(&demo, 0, cars, 3, hulls, &race, NULL) ==
		MH_ERROR_NONE);
	initial_race = race;
	CHECK(race.field.playback == 6 && !race.field.network &&
		race.field.active_mask == 7);
	CHECK(race.field.unavailable_mask == 0 && race.field.cars[0].remote &&
		race.field.cars[0].horn);
	CHECK(!race.field.cars[0].race.record_eligible &&
		race.field.cars[0].race.covered == 0);
	CHECK(mh_motion_display(&race.field.cars[0].vehicle.motion, position,
		velocity, NULL) == MH_ERROR_NONE);
	CHECK(position[0] == 50 &&
		fabs(position[1] - (double)(float).62) < 1e-7 &&
		velocity[0] == 0);
	memset(&controls, 0, sizeof(controls));
	controls.throttle = 1;
	controls.steering = 1;

	/* First six seconds hold frame zero, even with local driving input. */
	for (i = 0; i < 24; i++)
		CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps,
			NULL) == MH_ERROR_NONE &&
			steps == 7);

	CHECK(race.ticks == 168 && race.playback.anchor_ms == 0 &&
		race.field.cars[0].race.now_ms == 0);
	CHECK(memcmp(&race.field.cars[0].vehicle.motion,
		&initial_race.field.cars[0].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);

	/*
	 * The gate opens at 6000.5 relative ms, before integer time reaches
	 * 6001.
	 */
	CHECK(mh_demo_race_frame(&race, &controls, .0005, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(race.notice == 1 && race.playback.anchor_ms == 6001 &&
		race.notice_ms == 6001);
	saved_race = race;
	CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 7);
	CHECK(race.playback.target_ms == 250 &&
		race.field.cars[0].vehicle.motion.position[0] > 50);
	CHECK(race.field.cars[0].vehicle.engine.gear == 1 &&
		race.field.cars[0].horn);
	CHECK(race.field.cars[0].vehicle.wheel_rate[0] !=
		saved_race.field.cars[0].vehicle.wheel_rate[0]);
	CHECK(mh_wheel_frame(&saved_race.field.cars[0].wheels,
		race.field.cars[0].vehicle.wheel_rate,
		race.field.cars[0].frame.time.elapsed, &wheels,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&wheels, &race.field.cars[0].wheels, sizeof(wheels)) == 0);
	saved_race = race;
	saved_race.field.accumulator = 0;
	CHECK(mh_demo_race_frame(&race, &controls, 100, 1, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 0);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);
	steps = 99;
	CHECK(mh_demo_race_frame(&race, &controls, -1, 0, &steps, NULL) !=
			MH_ERROR_NONE &&
		steps == 99);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);

	/*
	 * Reject an unusable camera membership atomically after clock
	 * selection.
	 */
	pair_put32(data + MH_DEMO_HEADER_BYTES + 98, 0);
	CHECK(mh_demo_race_frame(&race, &controls, MH_DRIVE_DT, 0, &steps,
		NULL) != MH_ERROR_NONE &&
		steps == 99);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);
	pair_put32(data + MH_DEMO_HEADER_BYTES + 98, 7);

	/*
	 * Physics sees the old roster; playback then removes the viewed entrant
	 * before presentation/progress. It receives its last slice this frame.
	 */
	pair_put32(data + MH_DEMO_HEADER_BYTES + 98, 1);
	CHECK(mh_demo_race_frame(&race, &controls, MH_DRIVE_DT, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.active_mask == 2 && race.field.viewed_entry == 1);
	CHECK(race.field.listener_revision ==
		saved_race.field.listener_revision + 1);
	CHECK(race.field.cars[0].ticks == saved_race.field.cars[0].ticks + 1);
	CHECK(mh_demo_race_frame(&race, &controls, MH_DRIVE_DT, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.cars[0].ticks == saved_race.field.cars[0].ticks + 1);
	pair_put32(data + MH_DEMO_HEADER_BYTES + 98, 7);
	saved_race = race;
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);
	CHECK(race.ticks == saved_race.ticks && race.notice == 0 &&
		race.field.viewed_entry == 0 && race.playback.anchor_ms == 0);
	CHECK(race.field.cars[0].ticks == saved_race.field.cars[0].ticks &&
		race.field.cars[0].frame.seconds ==
			saved_race.field.cars[0].frame.seconds &&
		race.field.cars[0].frame.time.now_ms ==
			saved_race.field.cars[0].frame.time.now_ms &&
		race.field.cars[0].frame.time.elapsed ==
			saved_race.field.cars[0].frame.time.elapsed);
	CHECK(memcmp(&race.field.cars[0].vehicle,
		&initial_race.field.cars[0].vehicle,
		sizeof(struct mh_vehicle_state)) == 0);

	/*
	 * Saved best-lap time may exceed the clock at an interpolated crossing.
	 */
	race.field.cars[0].start_ticks = 1440;
	race.field.cars[0].race.now_ms = 79591;
	race.field.cars[0].race.best_ms = 79600;
	race.field.cars[0].race.best_lap = 1;
	CHECK(mh_field_classify(&race.field, NULL) == MH_ERROR_NONE &&
		race.field.standings.cars[0].best_ms == 79600);
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);

	/*
	 * Native finish correction can precede the local lap-start tick; held
	 * recorded splits can be ahead of the local clock, without unsigned
	 * expiry.
	 */
	race.field.cars[0].race.now_ms = 100;
	race.field.cars[0].race.lap_start_ms = 101;
	CHECK(mh_race_score(&race.field.cars[0].race, &score, NULL) !=
		MH_ERROR_NONE);
	race.field.cars[0].race.finished = 1;
	race.field.cars[0].race.split_ms = 102;
	CHECK(mh_race_score(&race.field.cars[0].race, &score, NULL) ==
			MH_ERROR_NONE &&
		score == 2147483547UL);
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 80; i++)
		CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps,
			NULL) == MH_ERROR_NONE);

	CHECK(!race.playback.active && race.notice == 2 &&
		race.playback.next_frame == 129);
	finished_playback = race.playback;
	CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&race.playback, &finished_playback,
		sizeof(finished_playback)) == 0);

	/*
	 * A long stall advances playback/GO by real elapsed time, despite the
	 * ten-slice physics cap. The original can exhaust its loaded ring
	 * before reading the rest of the file; do not invent catch-up playback
	 * calls.
	 */
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_race_frame(&race, &controls, 6.0005, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 10);
	CHECK(race.playback.anchor_ms ==
			(unsigned long)mh_drive_clock_ms(&race.field.cars[0]) &&
		race.playback.next_frame == 64);
	CHECK(mh_demo_race_frame(&race, &controls, 20, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 10);
	CHECK(!race.playback.active && race.playback.next_frame == 64 &&
		race.playback.target_ms == 20000);
	CHECK(race.field.cars[0].frame.time.elapsed == 1 &&
		race.field.cars[0].frame.time.go_ms ==
			(unsigned long)mh_drive_clock_ms(&race.field.cars[0]));
	CHECK(race.field.cars[0].race.now_ms == 0);
	CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 7);
	CHECK(race.field.cars[0].race.now_ms == 250);

	/*
	 * Eight recorded bodies yield seven ghosts; live human owns entry zero.
	 */
	CHECK(fixture(8, 7, &demo) == 0);
	demo.flags = 1;
	CHECK(mh_demo_race_begin(&demo, 1, cars, 8, hulls, &race, NULL) ==
		MH_ERROR_NONE);
	CHECK(race.field.tuning.catchup == 1);
	CHECK(mh_field_set_cheats(&race.field, MH_CHEAT_MOON, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < race.field.count; i++)
		race.field.cars[i].race.split_hold_ms = 4321 + i;

	CHECK(race.field.playback == 3 && !race.field.cars[0].remote &&
		race.field.cars[7].remote);
	CHECK(race.field.active_mask == 255 &&
		race.field.unavailable_mask == 254 &&
		race.playback.sample_count == 7);
	CHECK(mh_motion_display(&race.field.cars[7].vehicle.motion, position,
		velocity, NULL) == MH_ERROR_NONE &&
		position[0] == 70);
	memset(&records, 0, sizeof(records));
	CHECK(mh_field_bind_records(&race.field, &records, 1, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Force overlap with a ghost: local physics advances without any
	 * contact impulse or mutation of the recorded car.
	 */
	for (i = 0; i < 56; i++)
		CHECK(mh_demo_race_frame(&race, &controls, .25, 0, &steps,
			NULL) == MH_ERROR_NONE);

	CHECK(mh_drive_racing(&race.field.cars[0]));
	CHECK(mh_motion_display(&race.field.cars[1].vehicle.motion, position,
		velocity, NULL) == MH_ERROR_NONE);
	position[0] += 1.5;
	CHECK(mh_motion_reset(position, rotation,
		&race.field.cars[0].vehicle.motion,
		NULL) == MH_ERROR_NONE);
	race.field.cars[0].vehicle.motion.velocity[0] = -10;
	saved_race = race;

	/*
	 * Same clock repeats the same ghost sample; the original minimum dt
	 * still integrates the live car, without any ghost contact impulse.
	 */
	CHECK(mh_demo_race_frame(&race, &controls, 0, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(race.field.cars[0].vehicle.motion.position[0] !=
		saved_race.field.cars[0].vehicle.motion.position[0]);
	CHECK(race.field.contact_count[0] == 0 &&
		race.field.contact_count[1] == 0);
	CHECK(memcmp(&race.field.cars[1].vehicle,
		&saved_race.field.cars[1].vehicle,
		sizeof(struct mh_vehicle_state)) == 0);
	CHECK(memcmp(&race.field.records, &records, sizeof(records)) == 0);
	saved_race = race;
	steps = 99;
	controls.shift_up = 2;
	CHECK(mh_demo_race_frame(&race, &controls, .1, 0, &steps, NULL) !=
			MH_ERROR_NONE &&
		steps == 99);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);
	CHECK(mh_demo_race_restart(&race, NULL) == MH_ERROR_NONE);
	CHECK(race.field.records_bound && race.field.record_mask == 1 &&
		race.field.cars[0].race.record_eligible);
	CHECK(race.field.cars[1].race.record_eligible == 0 &&
		race.ticks == saved_race.ticks);
	CHECK(race.field.tuning.catchup == 1);

	for (i = 0; i < race.field.count; i++) {
		CHECK(race.field.cars[i].race.split_hold_ms == 4321 + i);
		CHECK(race.field.cars[i].cheats == MH_CHEAT_MOON);
	}

	puts("demo race: local mapping, clock gate, pose application, pause, restart, departures, EOF and ghost isolation passed");

	mh_collision_free(&col);
	mh_collision_free(&hulls[0]);

	return 0;
}
