#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/material.h"
#include "engine/assets/route.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field.h"

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
check_runtime_resets(const struct mh_drive *base)
{
	struct mh_drive drive, saved;
	struct mh_vehicle_state *vehicle;
	struct mh_vehicle_result tick;
	struct mh_vehicle_input input;
	struct mh_materials materials;
	unsigned int n, grid, i, j;
	struct mh_error error;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));
	input.controls.throttle = .5;
	materials = *base->materials;
	materials.count = 26;

	for (i = 1; i < materials.count; i++)
		materials.entries[i] = materials.entries[0];

	for (n = 0; n < 512; n++)
		for (grid = 0; grid < 2; grid++) {
			drive = *base;
			vehicle = &drive.vehicle;
			vehicle->engine.gear = 0;
			vehicle->engine.rpm = 2200 + n;
			vehicle->engine.up_ready = vehicle->engine.down_ready =
				0;
			vehicle->engine.detached = (n >> 4) & 1;
			vehicle->speed = (float)(n + 17);
			vehicle->grip.sliding = (n >> 5) & 1;
			vehicle->grip.phase = n + .25;
			vehicle->grip.slide_amount = n + .5;
			vehicle->grip.rear_limit = 2200 + n;
			vehicle->smoothed_steering = ((int)(n % 17) - 8) * .125;
			vehicle->body.contact = (n >> 6) & 1;
			vehicle->body.material = n % 26;
			vehicle->body.strong_air_damping = 1;
			vehicle->body_points = n & 1 ? 7 : 4;
			vehicle->brake_active = vehicle->heavy_brake =
				(n >> 7) & 1;
			vehicle->wheelspin = vehicle->engine_braking =
				(n >> 8) & 1;
			vehicle->contact_origin_y = n + .75;
			drive.body_impulse = 17000 + n;
			drive.body_effects.contact = (n >> 6) & 1;
			drive.body_effects.material = n % 26;
			drive.body_effects.impulse = 18000 + n;
			drive.impact_effects.contact = (n >> 7) & 1;
			drive.impact_effects.sound_class = n % 16;
			drive.impact_effects.severity = 30 + n;
			drive.recovery.roof_seconds = 1;
			drive.recovery.unsupported_seconds = 7;
			drive.recovery.ai_seconds = 6;
			drive.recovery.ai_latched = 1;
			drive.camera_roll.external = n & 1;
			drive.camera_roll.supported_seconds = (n % 17) * .0625f;
			drive.camera_mode = (enum mh_camera_mode)(n % 3);
			drive.look_back = (n >> 1) & 1;
			drive.camera.position[0] = 300 + n;
			drive.camera_focal = 2;
			drive.camera_far = 125;

			for (i = 0; i < 2; i++) {
				drive.body_effects.present[i] =
					(int)((n >> i) & 1);
				drive.body_effects.effect_material[i] =
					(n + i) % 26;
				drive.body_effects.scrape_speed[i] = n * .5 - i;
				drive.body_effects.corner[i] = (n + i) % 7;
				drive.body_sparks.id[i] = (n + i) % 2;
				drive.body_sparks.gain[i] =
					((int)(n % 9) - 1) * .125;
				drive.body_sparks.corner[i] = (n + i + 2) % 7;
				drive.wheels.angle[i] = n * .125 + i;
				drive.wheels.variant[i] = (n + i) % 4;
				vehicle->wheel_rate[i] =
					(n + 10) * (i == 0 ? -.5 : .25);
				vehicle->grip.tyre_effects.id[i] = (n + i) % 4;
				vehicle->grip.tyre_effects.gain[i] =
					(n % 9) * .125;
			}

			for (i = 0; i < 3; i++) {
				vehicle->motion.velocity[i] = n * .125 + i;
				vehicle->motion.angular_velocity[i] =
					n * .0625 - i;
				vehicle->motion.previous_velocity[i] =
					n * .25 - i;
				vehicle->motion.previous_angular_velocity[i] =
					n * .03125 + i;
			}

			for (i = 0; i < 4; i++) {
				vehicle->motion.extension[i] = (n % 9) * .125;
				vehicle->grip.wheels[i].supported =
					(n >> i) & 1;
				vehicle->grip.wheels[i].material = (n + i) % 26;
				vehicle->grip.wheels[i].normal_y = .75;
				vehicle->wheel_contacts[i].hit = 1;
				vehicle->wheel_contacts[i].polygon = n + i;
				vehicle->wheel_contacts[i].fraction = .25;
				vehicle->wheel_contacts[i].plane_distance =
					n * .5;
				vehicle->wheel_contacts[i].material =
					(n + i) % 26;

				for (j = 0; j < 3; j++) {
					vehicle->wheel_contacts[i].normal[j] =
						j == 1 ? .75 : .5;
					vehicle->wheel_contacts[i].position[j] =
						n * .125 + i + j;
				}
			}

			saved = drive;

			CHECK(mh_drive_reset(&drive, (int)grid, NULL) ==
				MH_ERROR_NONE);

			CHECK(memcmp(vehicle->wheel_contacts,
				saved.vehicle.wheel_contacts,
				sizeof(vehicle->wheel_contacts)) == 0);
			CHECK(memcmp(vehicle->grip.wheels,
				saved.vehicle.grip.wheels,
				sizeof(vehicle->grip.wheels)) == 0);
			CHECK(drive.body_impulse == 0);
			CHECK(!drive.camera_roll.external &&
				drive.camera_roll.supported_seconds == 0);
			CHECK(memcmp(&drive.camera, &saved.camera,
				sizeof(drive.camera)) == 0);
			CHECK(memcmp(drive.eye, saved.eye, sizeof(drive.eye)) ==
				0);
			CHECK(memcmp(drive.forward, saved.forward,
				sizeof(drive.forward)) == 0);
			CHECK(memcmp(drive.target, saved.target,
				sizeof(drive.target)) == 0);
			CHECK(drive.camera_mode == saved.camera_mode &&
				drive.look_back == saved.look_back);
			CHECK(drive.camera_focal == 2 &&
				drive.camera_far == 125);
			CHECK(memcmp(&drive.body_effects, &saved.body_effects,
				sizeof(drive.body_effects)) == 0);
			CHECK(memcmp(&drive.impact_effects,
				&saved.impact_effects,
				sizeof(drive.impact_effects)) == 0);
			CHECK(memcmp(&drive.body_sparks, &saved.body_sparks,
				sizeof(drive.body_sparks)) == 0);
			CHECK(vehicle->engine.detached ==
				saved.vehicle.engine.detached);
			CHECK(vehicle->smoothed_steering ==
				saved.vehicle.smoothed_steering);
			CHECK(vehicle->body.contact ==
					saved.vehicle.body.contact &&
				vehicle->body.material ==
					saved.vehicle.body.material);
			CHECK(vehicle->body.strong_air_damping == 0 &&
				vehicle->engine.gear == 1 &&
				vehicle->engine.rpm == base->config.min_rpm);
			CHECK(mh_wheel_frame(&drive.wheels, vehicle->wheel_rate,
				.025f, &drive.wheels,
				NULL) == MH_ERROR_NONE);

			status = mh_vehicle_step(base->car, base->col,
				&materials, &drive.config, vehicle, &input,
				.025, &tick, &error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "reset %u/%u: %s\n", n, grid,
					error.message);
				failures++;
			}

			saved = drive;
			drive.config.gears = 0;
			CHECK(mh_drive_reset(&drive, (int)grid, NULL) !=
				MH_ERROR_NONE);
			drive.config = saved.config;
			CHECK(memcmp(&drive, &saved, sizeof(drive)) == 0);
		}
}

static void
check_timers(void)
{
	struct mh_recovery_state state;
	struct mh_recovery_feedback feedback;
	struct mh_recovery_result result, saved;
	unsigned int mask, i, tick;

	/* Independent mask classifications in FL FR BR BL order. */
	static const unsigned int advance[] = {
		1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0};

	for (mask = 0; mask < 16; mask++) {
		memset(&state, 0, sizeof(state));
		memset(&feedback, 0, sizeof(feedback));
		feedback.racing = 1;

		for (i = 0; i < 4; i++)
			feedback.supported[i] = (mask >> i) & 1;

		for (tick = 0; tick < 16; tick++) {
			CHECK(mh_recovery_step(&state, &feedback, .5, &result,
				NULL) == MH_ERROR_NONE);
			CHECK(result.reason == MH_RECOVERY_NONE);
			state = result.state;
		}

		NEAR(state.unsupported_seconds, advance[mask] * 8);
		CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE);
		CHECK(result.reason ==
			(advance[mask] ? MH_RECOVERY_SUPPORT
				: MH_RECOVERY_NONE));
	}

	memset(&feedback, 0, sizeof(feedback));
	feedback.racing = 1;
	feedback.roof_contact = 1;
	state.roof_seconds = 1.5;
	state.unsupported_seconds = 7.5;
	CHECK(mh_recovery_step(&state, &feedback, 1000, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.state.roof_seconds, 2);
	NEAR(result.state.unsupported_seconds, 8);
	CHECK(result.reason == MH_RECOVERY_NONE);
	state = result.state;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_ROOF);
	NEAR(result.state.unsupported_seconds, 8);

	/* A lost roof contact holds its timer; it does not restart it. */
	feedback.roof_contact = 0;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_SUPPORT);
	NEAR(result.state.roof_seconds, 2);

	/*
	 * Same-side support holds both accumulated times. Three wheels clear.
	 */
	feedback.supported[0] = feedback.supported[3] = 1;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE);
	NEAR(result.state.unsupported_seconds, 8);
	feedback.supported[1] = 1;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	NEAR(result.state.roof_seconds, 0);
	NEAR(result.state.unsupported_seconds, 0);

	/* Roof expiration precedes even the three-wheel reset. */
	feedback.roof_contact = 1;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_ROOF);
	feedback.racing = 0;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE);
	NEAR(result.state.roof_seconds, 2.125);
	feedback.racing = feedback.finished = 1;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE);
	feedback.position[1] = -100;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE);
	feedback.position[1] = -100.001f;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_FALL);
	feedback.racing = 0;
	CHECK(mh_recovery_step(&state, &feedback, .125, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_FALL);
	saved = result;

	for (i = 0; i < 7; i++) {
		struct mh_recovery_feedback bad;
		struct mh_recovery_state bad_state;
		double seconds;

		bad = feedback;
		bad_state = state;
		seconds = .1;

		if (i == 0)
			bad.supported[3] = -1;

		if (i == 1)
			bad.roof_contact = 2;

		if (i == 2)
			bad.position[1] = (float)HUGE_VAL;

		if (i == 3)
			bad_state.roof_seconds = -1;

		if (i == 4)
			bad_state.unsupported_seconds = HUGE_VAL;

		if (i == 5)
			seconds = 0;

		if (i == 6)
			seconds = HUGE_VAL;

		CHECK(mh_recovery_step(&bad_state, &bad, seconds, &result,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	}
}

static void
check_navigation_case(const struct mh_drive *base,
	const struct mh_ai_path *path, const struct mh_engine_input *input,
	unsigned int local_ai, unsigned int slot, unsigned int reason)
{
	struct mh_field field, saved;
	struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_roster_result expected;
	struct mh_drive *drive;
	double position[3], velocity[3];
	unsigned int i, j;
	struct mh_error error;
	enum mh_error_code status;

	memset(&field, 0, sizeof(field));
	field.path = path;
	field.count = 8;
	field.active_mask = field.ai_roster.active_mask = 255;
	field.ai_random = 731;

	for (i = 0; i < 8; i++) {
		drive = &field.cars[i];
		*drive = *base;
		drive->slot = i;

		if (i || local_ai)
			drive->ai_path = path;

		CHECK(mh_drive_reset(drive, 1, NULL) == MH_ERROR_NONE);
		drive->opponent.driver.mass = base->car->mass;
		drive->opponent.driver.corner_grip =
			drive->opponent.driver.corner_steering = 1;
		drive->opponent.vehicle.turn_force = base->car->turn_force;
		drive->opponent.vehicle.brake_force = base->car->brake_force;
		drive->opponent.vehicle.grip_level = base->car->grip_level;
		drive->ai.state.point = i;
		drive->ai.state.corner = (i + 3) % 32;
		drive->ai.state.brake_points = i + 1;
		drive->ai.state.lane = i * .125;
		drive->ai.state.previous_lateral = -(double)i * .25;
		drive->ai.state.throttle_limit = .75;
		drive->ai.state.stuck_seconds = .625;
		drive->ai.state.turn_latch = 1;
		drive->ai.state.reversing = drive->ai.state.offroad = 1;
		drive->ai.steering = .25;
		drive->ai.throttle = .5;
		drive->ai.lateral_rate = 3;
		drive->vehicle.wheelspin = 1;
		CHECK(mh_body_effect_impact(&drive->body_effects, 0, i % 7,
			18000 + i, 1, 20, NULL) == MH_ERROR_NONE);
		CHECK(mh_impact_add(&drive->impact_effects, i % 16, 30 + i,
			NULL) == MH_ERROR_NONE);
		drive->body_impulse = 19000 + i;
		drive->body_sparks.id[0] = 1;
		drive->body_sparks.gain[0] = .5;
		drive->body_sparks.corner[0] = i % 7;
		drive->ai_personality_enabled = drive->ai_path != NULL;
		drive->ai_boosted = 1;
		drive->ai_parameters[0] = .25f;
		drive->ai_parameters[1] = .75f;
		drive->ai_parameters[2] = .5f;
		drive->ai_personality.walk[0] = .25;
		drive->ai_personality.walk[1] = .5;
		drive->ai_personality.walk[2] = .75;
		field.ai_roster.order[i] = 7 - i;
		field.point[i] = i;
		field.ai_roster.cars[i].point = i;
		field.ai_roster.cars[i].along = i + .125;
		field.ai_roster.cars[i].lane = i * .125;
		field.ai_roster.cars[i].lateral = -(double)i * .25;
		field.ai_roster.cars[i].speed = 20 + i;
		field.ai_roster.cars[i].heading_dot = .5;
		field.ai_roster.cars[i].lateral_rate = field.lateral_rate[i] =
			3 + i;
		field.ai_roster.cars[i].width = 2.1;
		field.ai_roster.cars[i].length = 4.4;
		field.traffic[i].threat = (int)((i + 1) % 8);
		field.traffic[i].lane = i * .125;
		field.traffic[i].brake = field.traffic[i].changing = 1;

		for (j = 0; j < MH_CAR_BODY_POINTS; j++) {
			field.body_contacts[i][j].hit = (int)((i + j) & 1);
			field.body_contacts[i][j].normal[(i + j) % 3] = 1;
			field.body_contacts[i][j].position[0] = i + j * .125;
			field.body_contacts[i][j].material = i + j;
			field.body_contacts[i][j].polygon = 32 * i + j;
			field.body_contacts[i][j].fraction = .25;
			field.body_contacts[i][j].plane_distance = i + j * .5;
		}
	}

	drive = &field.cars[slot];

	if (reason == 1)
		drive->vehicle.motion.position[1] = -101;

	if (reason == 2) {
		drive->recovery.roof_seconds = 2;
		drive->vehicle.body.strong_air_damping = 1;
	}

	if (reason == 3)
		drive->recovery.unsupported_seconds = 8;

	if (reason == 4) {
		drive->recovery.ai_latched = 1;
		drive->recovery.ai_seconds = 8;
		field.cars[0].camera.position[0] = -1000;
	}

	saved = field;
	status = reason ? mh_field_frame_update(&field, .125f, &error)
			: mh_field_reset(&field, 0, &error);

	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	CHECK(field.cars[slot].recovery_event ==
		(enum mh_recovery_reason)reason);
	CHECK(memcmp(field.body_contacts, saved.body_contacts,
		sizeof(field.body_contacts)) == 0);
	CHECK(memcmp(&field.ai_roster, &saved.ai_roster,
		sizeof(field.ai_roster)) == 0);
	CHECK(memcmp(field.traffic, saved.traffic, sizeof(field.traffic)) == 0);
	CHECK(memcmp(field.point, saved.point, sizeof(field.point)) == 0);
	CHECK(memcmp(field.lateral_rate, saved.lateral_rate,
		sizeof(field.lateral_rate)) == 0);
	CHECK(field.ai_random == saved.ai_random);
	memset(drivers, 0, sizeof(drivers));

	for (i = 0; i < 8; i++) {
		drive = &field.cars[i];
		CHECK(memcmp(&drive->ai, &saved.cars[i].ai,
			sizeof(drive->ai)) == 0);
		CHECK(memcmp(&drive->ai_personality,
			&saved.cars[i].ai_personality,
			sizeof(drive->ai_personality)) == 0);
		CHECK(drive->vehicle.wheelspin == 1);
		CHECK(drive->body_impulse ==
			(i == slot ? 0 : saved.cars[i].body_impulse));
		CHECK(memcmp(&drive->body_effects, &saved.cars[i].body_effects,
			sizeof(drive->body_effects)) == 0);
		CHECK(memcmp(&drive->impact_effects,
			&saved.cars[i].impact_effects,
			sizeof(drive->impact_effects)) == 0);
		CHECK(memcmp(&drive->body_sparks, &saved.cars[i].body_sparks,
			sizeof(drive->body_sparks)) == 0);
		CHECK(drive->ai_boosted == 1);
		CHECK(mh_motion_display(&drive->vehicle.motion, position,
			velocity, NULL) == MH_ERROR_NONE);
		drivers[i].ai = drive->ai_path != NULL;
		drivers[i].state = saved.cars[i].ai.state;

		if (!drivers[i].ai) {
			drivers[i].state.point = drivers[i].state.corner =
				saved.ai_roster.cars[i].point;
			drivers[i].state.lane = saved.ai_roster.cars[i].lane;
			drivers[i].state.throttle_limit = 1;
		}

		drivers[i].config = drive->opponent.driver;
		drivers[i].personality = drive->ai_personality;
		drivers[i].personality_enabled = drive->ai_personality_enabled;
		memcpy(drivers[i].parameters, drive->ai_parameters,
			sizeof(drivers[i].parameters));
		drivers[i].feedback.boosted =
			drive->ai_personality_enabled ? drive->ai_boosted : 0;
		drivers[i].feedback.x = position[0];
		drivers[i].feedback.z = position[2];
		drivers[i].feedback.heading_x =
			drive->vehicle.motion.rotation[6];
		drivers[i].feedback.heading_z =
			drive->vehicle.motion.rotation[8];
		drivers[i].feedback.speed = drive->vehicle.motion.velocity[2];
		drivers[i].feedback.wheelspin = 1;
	}

	/* The next whole pass consumes recovered poses and retained samples. */
	status = mh_ai_roster_step(path, &saved.ai_roster, drivers,
		saved.ai_random, MH_DRIVE_DT, &expected, &error);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "recovery roster: %s\n", error.message);
		return;
	}

	status = mh_field_frame_tick(&field, input, 1, &error);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "recovery field: %s\n", error.message);
		return;
	}

	CHECK(memcmp(&field.ai_roster, &expected.history,
		sizeof(field.ai_roster)) == 0);
	CHECK(memcmp(field.traffic, expected.traffic,
		sizeof(expected.traffic)) == 0);

	for (i = 0; i < 8; i++) {
		if (drivers[i].ai)
			CHECK(memcmp(&field.cars[i].ai, &expected.drivers[i],
				sizeof(field.cars[i].ai)) == 0);

		CHECK(memcmp(&field.cars[i].ai_personality,
			&expected.personality[i],
			sizeof(expected.personality[i])) == 0);
		CHECK(field.cars[i].ai_boosted == expected.boosted[i]);
	}

	CHECK(field.ai_random == expected.random);
}

static void
check_navigation_history(const struct mh_drive *base)
{
	struct mh_ai_point points[32];
	struct mh_ai_section section;
	struct mh_ai_path path;
	struct mh_engine_input input;
	double angle;
	unsigned int i, local_ai, slot, reason;

	memset(points, 0, sizeof(points));
	memset(&section, 0, sizeof(section));
	memset(&path, 0, sizeof(path));
	memset(&input, 0, sizeof(input));
	path.points = points;
	path.count = 32;
	path.sections = &section;
	path.section_count = 1;
	section.count = 32;
	section.radius = 100;

	for (i = 0; i < 32; i++) {
		angle = i * 6.283185307179586 / 32;
		points[i].x = 100 * sin(angle);
		points[i].z = 100 * cos(angle);
		points[i].direction[0] = cos(angle);
		points[i].direction[2] = -sin(angle);
		points[i].width[0] = points[i].width[1] = 8;
		points[i].radius = 100;
	}

	for (local_ai = 0; local_ai < 2; local_ai++)
		for (slot = 0; slot < 8; slot++)
			for (reason = 0; reason < 5; reason++) {
				if ((!reason && slot) ||
					(reason == 4 && !slot && !local_ai))
					continue;

				check_navigation_case(base, &path, &input,
					local_ai, slot, reason);
			}
}

static void
check_sessions(void)
{
	struct mh_car car;
	struct mh_collision col;
	struct mh_materials materials;
	struct mh_track track;
	struct mh_route route;
	struct mh_drive base, drive, expected, saved;
	struct mh_field field, reference, saved_field;
	struct mh_ai_path path;
	struct mh_ai_point points[2];
	struct mh_engine_input input;
	char text[8192];
	size_t used;
	unsigned int i, steps;
	double angle;

	memset(&car, 0, sizeof(car));
	memset(&col, 0, sizeof(col));
	memset(&materials, 0, sizeof(materials));
	memset(&track, 0, sizeof(track));
	memset(&input, 0, sizeof(input));
	car.has_suspension = car.has_steering = car.has_grip = car.has_corners =
		car.has_engine = 1;
	car.mass = 1450;
	car.spring_length = .4;
	car.spring_strength = 50000;
	car.turn_force = 12000;
	car.min_rpm = 1000;
	car.max_rpm = 10000;
	car.brake_force = .5;
	car.gears = 1;
	car.gear_ratio[0] = 50;
	car.gear_ratio[1] = 70;
	car.acceleration_force = 2800;
	car.acceleration_level = car.speed_level = car.grip_level = 5;
	CHECK(mh_materials_mat_srf_decode(
		(const unsigned char *)"Materials 1\nMaterialName road\n",
		30, (const unsigned char *)"Materials 1\nMaterial road\n",
		26, &materials, NULL) == MH_ERROR_NONE);

	/* An empty synthetic collision world: every cast misses. */
	col.scale = 1;
	track.has_start_grid = 1;
	track.start_grid[0] = 12;
	track.start_grid[1] = 6;
	track.start_grid[2] = 3;
	track.start_grid[3] = 5.5;
	track.laps = 3;
	strcpy(text, "LWMO\n1\n9\n33\n");
	used = strlen(text);

	for (i = 0; i < 33; i++) {
		angle = (i % 32) * 6.283185307179586 / 32;
		used += sprintf(text + used,
			"%.17g 0 %.17g 0 0 0 1 1 1\n%u 1 0 0 0\n",
			100 * sin(angle), 100 * cos(angle), i * 10);
	}

	CHECK(mh_route_mot_parse((const unsigned char *)text, used, &route,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_start(&car, &col, &materials, &route, &track, 0, 0, 0,
		&base, NULL) == MH_ERROR_NONE);
	check_runtime_resets(&base);

	check_navigation_history(&base);
	base.ticks = 2000;
	base.seconds = 20;
	base.race_ticks = 800;
	base.race.now_ms = 6666;
	base.race.best_ms = 5000;
	base.race.best_lap = 1;
	base.race.record_eligible = 1;
	drive = base;
	drive.recovery.roof_seconds = 2;
	drive.vehicle.body.strong_air_damping = 1;

	for (i = 0; i < 6; i++)
		drive.vehicle.motion.rotation[i] *= -1;

	drive.accumulator = .025;
	expected = drive;
	CHECK(mh_drive_reset(&expected, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_recovery_frame(&drive, base.camera.position,
		(float)MH_DRIVE_DT, NULL) == MH_ERROR_NONE);
	CHECK(drive.recovery_event == MH_RECOVERY_ROOF);
	CHECK(memcmp(&drive.camera, &base.camera, sizeof(base.camera)) == 0);
	CHECK(memcmp(&drive.vehicle, &expected.vehicle,
		sizeof(drive.vehicle)) == 0);
	CHECK(memcmp(&drive.race, &expected.race, sizeof(drive.race)) == 0);
	CHECK(drive.race_ticks == 800 && drive.ticks == 2000 &&
		drive.seconds == 20);
	CHECK(drive.race.best_ms == 5000 && drive.race.best_lap == 1);
	CHECK(drive.race.covered == base.race.covered &&
		!drive.race.lap_event && !drive.race.split_event);
	CHECK(drive.race.record_eligible == 0 &&
		base.race.record_eligible == 1);
	NEAR(drive.accumulator, .025);
	NEAR(drive.recovery.roof_seconds, 0);

	/* Invalid physics input preserves the frame before recovery can run. */
	drive = base;
	drive.vehicle.motion.position[1] = -101;
	saved = drive;
	input.handbrake = 2;
	CHECK(mh_drive_tick(&drive, &input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&drive, &saved, sizeof(drive)) == 0);
	input.handbrake = 0;
	expected = drive;
	CHECK(mh_drive_advance(&drive, &input, .025, 0, &steps, NULL) ==
		MH_ERROR_NONE);
	CHECK(steps == 1);

	/*
	 * One sampled .025-second slice, as in the original frame scheduler.
	 * Reset now preserves its feedback, so three diagnostic ticks differ.
	 */
	CHECK(mh_drive_frame_begin(&expected, .025, NULL) == MH_ERROR_NONE);
	expected.frame.step = expected.frame.time.elapsed;
	CHECK(mh_drive_integrate_tick(&expected, &input, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_drive_recovery_frame(&expected, saved.camera.position, .025f,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&drive.vehicle, &expected.vehicle,
		sizeof(drive.vehicle)) == 0);

	/*
	 * The binary64 clock product is just below 20026 ms; subtracting the
	 * integer GO epoch (1 ms) and truncating yields 20024, not 20025.
	 */
	CHECK(drive.ticks == 2001 && drive.race.now_ms == 20024);
	NEAR(drive.accumulator, 0);
	CHECK(drive.race.record_eligible == 0);

	/*
	 * The fall reset is followed by one predicate call on the new
	 * unsupported car. Its timer receives the whole frame interval, not
	 * three substeps.
	 */
	CHECK(drive.recovery_event == MH_RECOVERY_FALL);
	CHECK(drive.recovery.unsupported_seconds == .025f);
	CHECK(drive.recovery_sample.fell &&
		drive.recovery_sample.elapsed == .025f);
	CHECK(mh_drive_reset(&drive, 1, NULL) == MH_ERROR_NONE);
	NEAR(drive.recovery.unsupported_seconds, 0);
	CHECK(drive.recovery_event == MH_RECOVERY_NONE);

	/*
	 * Stale roof feedback must not reset before this frame can land/clear
	 * it.
	 */
	drive = base;
	drive.recovery.roof_seconds = 2;
	drive.vehicle.body.strong_air_damping = 1;
	CHECK(mh_drive_tick(&drive, &input, NULL) == MH_ERROR_NONE &&
		drive.recovery_event == MH_RECOVERY_NONE);
	CHECK(drive.recovery.roof_seconds == 2 &&
		drive.recovery_sample.feedback.roof_contact == 0);
	drive = base;
	drive.vehicle.motion.position[1] = -101;

	for (i = 0; i < 3; i++)
		drive.vehicle.grip.wheels[i].supported = 1;

	drive.ai.state.reversing = 1;
	CHECK(mh_drive_recovery_frame(&drive, base.camera.position, .1f,
		NULL) == MH_ERROR_NONE);
	CHECK(drive.recovery_event == MH_RECOVERY_FALL &&
		drive.recovery.unsupported_seconds == 0);
	CHECK(drive.recovery_sample.feedback.reversing == 1);

	for (i = 0; i < 3; i++)
		CHECK(drive.recovery_sample.feedback.supported[i]);

	drive = base;
	drive.cheats = MH_CHEAT_UNDERWATER;
	CHECK(mh_drive_advance(&drive, &input, .025, 0, &steps, NULL) ==
			MH_ERROR_NONE &&
		steps == 1);
	CHECK(drive.recovery_sample.elapsed == .025f &&
		drive.recovery.unsupported_seconds == .025f);

	/*
	 * All physics finishes before a single frame-end recovery. Its
	 * preceding traffic samples survive until the next control pass.
	 */
	memset(&field, 0, sizeof(field));
	memset(&path, 0, sizeof(path));
	memset(points, 0, sizeof(points));
	path.count = 2;
	path.points = points;
	points[0].direction[2] = points[1].direction[2] = 1;
	points[1].z = 100;
	field.path = &path;
	field.count = 2;
	field.active_mask = 3;
	field.cars[0] = field.cars[1] = base;
	field.cars[1].vehicle.motion.position[1] = -101;
	field.lateral_rate[1] = 20;
	field.traffic[1].brake = 1;
	reference = field;
	CHECK(mh_field_frame_tick(&reference, &input, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(reference.cars[1].recovery_event == MH_RECOVERY_NONE);
	CHECK(reference.cars[1].vehicle.motion.position[1] < -100);
	saved_field = reference;
	CHECK(mh_field_frame_update(&reference, (float)MH_DRIVE_DT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_field_tick(&field, &input, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[1].recovery_event == MH_RECOVERY_FALL);

	for (i = 0; i < 2; i++)
		CHECK(memcmp(&field.cars[i].vehicle, &reference.cars[i].vehicle,
			sizeof(base.vehicle)) == 0);

	CHECK(field.point[1] == saved_field.point[1]);
	CHECK(memcmp(&field.ai_roster, &saved_field.ai_roster,
		sizeof(field.ai_roster)) == 0);
	NEAR(field.lateral_rate[1], saved_field.lateral_rate[1]);

	/*
	 * Every car step leaves timers alone. Only final frame feedback counts.
	 */
	field.cars[1].recovery.unsupported_seconds = 8;

	for (i = 0; i < 3; i++)
		CHECK(mh_field_frame_tick_duration(&field, &input, i == 0,
			i == 0 ? .025 : 0, NULL) == MH_ERROR_NONE);

	CHECK(field.cars[1].recovery.unsupported_seconds == 8 &&
		field.cars[1].recovery_event == MH_RECOVERY_NONE);

	for (i = 0; i < 3; i++)
		field.cars[1].vehicle.grip.wheels[i].supported = 1;

	CHECK(mh_field_frame_update(&field, .025f, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[1].recovery.unsupported_seconds == 0 &&
		field.cars[1].recovery_event == MH_RECOVERY_NONE);

	/*
	 * A later car's invalid recovery state rolls back an earlier fall
	 * reset.
	 */
	field.cars[0].vehicle.motion.position[1] = -101;
	field.cars[1].recovery.unsupported_seconds = -1;
	saved_field = field;
	CHECK(mh_field_frame_update(&field, .025f, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field, &saved_field, sizeof(field)) == 0);
	field.cars[1].recovery.unsupported_seconds = 0;
	field.cars[0].vehicle.motion.position[1] = -101;
	field.cars[1].config.gears = 0;
	saved_field = field;
	CHECK(mh_field_tick(&field, &input, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&field, &saved_field, sizeof(field)) == 0);
	mh_route_free(&route);
}

static void
check_ai_recovery(void)
{
	struct mh_recovery_state state;
	struct mh_recovery_feedback feedback;
	struct mh_recovery_result result, saved;
	unsigned int i;

	memset(&state, 0, sizeof(state));
	memset(&feedback, 0, sizeof(feedback));
	feedback.racing = feedback.ai_controlled = feedback.reversing = 1;
	feedback.position[0] = 151;

	for (i = 0; i < 4; i++)
		feedback.supported[i] = 1;

	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.state.ai_latched && result.state.ai_seconds == .5f &&
		result.reason == MH_RECOVERY_NONE);
	feedback.reversing = 0;
	state = result.state;

	for (i = 0; i < 15; i++) {
		CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE);
		state = result.state;
	}

	CHECK(result.state.ai_seconds == 8 &&
		result.reason == MH_RECOVERY_NONE);

	/*
	 * Compare unrounded sum even when the stored float still equals eight.
	 */
	CHECK(mh_recovery_step(&state, &feedback, 1e-8, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_AI && !result.state.ai_latched &&
		result.state.ai_seconds == 8);
	feedback.position[0] = 150;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE &&
		result.state.ai_seconds == 8.5f && result.state.ai_latched);
	feedback.position[0] = 0;
	feedback.position[2] = 151;
	feedback.camera[2] = 1;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE &&
		result.reason == MH_RECOVERY_NONE);
	feedback.camera[2] = 0;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE &&
		result.reason == MH_RECOVERY_AI);
	feedback.forward_speed = -15;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE &&
		result.reason == MH_RECOVERY_AI);
	feedback.forward_speed = -15.000001f;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE && !result.state.ai_latched &&
		result.state.ai_seconds == 8);
	feedback.reversing = 1;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE && result.state.ai_latched &&
		result.state.ai_seconds == .5f);
	feedback.forward_speed = 0;
	feedback.reversing = 0;
	feedback.ai_controlled = 0;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.reason == MH_RECOVERY_NONE &&
		memcmp(&result.state, &state, sizeof(state)) == 0);
	feedback.ai_controlled = 1;
	feedback.racing = 0;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE &&
		result.reason == MH_RECOVERY_NONE && !result.state.ai_latched);
	feedback.racing = feedback.finished = 1;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) ==
			MH_ERROR_NONE &&
		result.reason == MH_RECOVERY_NONE && !result.state.ai_latched);
	saved = result;
	feedback.camera[2] = (float)HUGE_VAL;
	CHECK(mh_recovery_step(&state, &feedback, .5, &result, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&result, &saved, sizeof(result)) == 0);
}

int
main(void)
{

	check_timers();
	check_ai_recovery();
	check_sessions();

	if (failures)
		return 1;

	puts("Recovery masks, strict timers, gates, relocation, clocks, frame backlog and field rollback passed");

	return 0;
}
