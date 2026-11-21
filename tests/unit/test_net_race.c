#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../support/pair_fixture.h"
#include "engine/assets/col.h"
#include "engine/assets/material.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/network/race.h"
#include "engine/simulation/tick_clock.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)
static struct mh_net_session session, saved_session;
static struct mh_net_race race, saved_race;
static struct mh_net_race grid_race;
static struct mh_net_session grid_session;

static void
unavailable_contacts(const struct mh_field *source)
{
	static struct mh_field initial, field;
	struct mh_engine_input controls;
	unsigned int i;
	double position[3] = {0, 10, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	initial = *source;
	initial.active_mask = 3;
	memset(&controls, 0, sizeof(controls));
	CHECK(mh_motion_reset(position, rotation,
		&initial.cars[1].vehicle.motion, NULL) == MH_ERROR_NONE);
	position[0] = 1.5;
	CHECK(mh_motion_reset(position, rotation,
		&initial.cars[0].vehicle.motion, NULL) == MH_ERROR_NONE);
	initial.cars[0].vehicle.motion.velocity[0] = -10;
	initial.cars[0].start_ticks = initial.cars[1].start_ticks = 1680;
	field = initial;
	CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);
	CHECK(field.contact_count[0] > 0 && field.contact_count[1] > 0);
	field = initial;
	field.unavailable_mask = 2;
	CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);
	CHECK(field.contact_count[0] == 0 && field.contact_count[1] == 0);
	CHECK(memcmp(&field.cars[1].vehicle.motion,
		&initial.cars[1].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);
	field = initial;
	field.unavailable_mask = 1;
	CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);
	CHECK(field.contact_count[0] == 0 && field.contact_count[1] == 0);
	CHECK(memcmp(&field.cars[0].vehicle.motion,
		&initial.cars[0].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);
	CHECK(field.active_mask == 3 && field.standings.cars[0].position != 0 &&
		field.standings.cars[1].position != 0);

	/*
	 * Engine/steering and wheel presentation are separate from membership
	 * in the original physics list. Remote engines remain snapshot-owned.
	 */
	initial.cars[0].config.options.manual = 1;
	initial.cars[0].config.gears = 2;
	initial.cars[0].config.ratio[2] = 140;
	initial.cars[0].vehicle.wheel_rate[0] = 4;
	initial.cars[0].vehicle.wheel_rate[1] = 8;
	field = initial;
	field.unavailable_mask = 3;
	controls.throttle = 1;
	controls.steering = .5;
	controls.shift_up = 1;

	for (i = 0; i < 12; i++)
		CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);

	CHECK(field.cars[0].vehicle.engine.rpm >
		initial.cars[0].vehicle.engine.rpm);
	CHECK(field.cars[0].vehicle.engine.gear == 1 &&
		!field.cars[0].vehicle.engine.up_ready);
	CHECK(field.cars[0].vehicle.smoothed_steering > .4 &&
		field.cars[0].sound_pedal == 1);
	CHECK(field.cars[0].wheels.angle[0] != initial.cars[0].wheels.angle[0]);
	controls.shift_up = 0;
	controls.throttle = 0;
	controls.brake = 1;
	CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].vehicle.engine.gear == 2 &&
		field.cars[0].vehicle.engine.up_ready);
	CHECK(field.cars[0].vehicle.heavy_brake &&
		field.cars[0].vehicle.brake_active &&
		field.cars[0].sound_pedal == 0);
	CHECK(memcmp(&field.cars[0].vehicle.motion,
		&initial.cars[0].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);
	CHECK(memcmp(&field.cars[0].vehicle.grip, &initial.cars[0].vehicle.grip,
		sizeof(struct mh_grip_state)) == 0);
	CHECK(memcmp(&field.cars[0].vehicle.body, &initial.cars[0].vehicle.body,
		sizeof(struct mh_body_feedback)) == 0);
	CHECK(memcmp(field.cars[0].vehicle.wheel_rate,
		initial.cars[0].vehicle.wheel_rate,
		sizeof(double) * 2) == 0);
	CHECK(memcmp(&field.cars[1].vehicle, &initial.cars[1].vehicle,
		sizeof(struct mh_vehicle_state)) == 0);
	CHECK(field.cars[0].ticks == initial.cars[0].ticks + 13 &&
		field.cars[1].ticks == initial.cars[1].ticks + 13);

	/*
	 * Recovery still runs for an unavailable local car and preserves its
	 * elimination status and, on the host, frozen race classification.
	 */
	field.cars[0].recovery.unsupported_seconds = 8;
	field.cars[0].race_frozen = 1;
	field.cars[0].race.sound_blend = .375f;
	field.cars[0].race.record_eligible = 1;
	CHECK(mh_field_tick(&field, &controls, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].recovery_event == MH_RECOVERY_SUPPORT &&
		field.cars[0].race_frozen);
	CHECK(field.cars[0].race.record_eligible == 0);
	CHECK(field.cars[0].race.sound_blend == .375f);
	CHECK(field.unavailable_mask == 3 &&
		field.cars[0].recovery.unsupported_seconds == 0);

	/*
	 * Invalid controls must fail even without road integration, atomically.
	 */
	initial.unavailable_mask = 1;
	field = initial;
	controls.shift_up = 2;
	CHECK(mh_field_tick(&field, &controls, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &initial, sizeof(field)) == 0);
	controls.shift_up = 0;
	initial.unavailable_mask = 1;
	initial.cars[0].ticks = ULONG_MAX;
	field = initial;
	CHECK(mh_field_tick(&field, &controls, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &initial, sizeof(field)) == 0);
}

/*
 * Place a synthetic frame at an exact clock without integrating the skipped
 * interval. This fixture does not claim physical driving over that interval.
 */
static void
clock_at(double seconds, unsigned long intro, unsigned long go)
{
	unsigned int i;

	for (i = 0; i < race.field.count; i++) {
		race.field.cars[i].frame.active = 1;
		race.field.cars[i].frame.seconds = seconds;
		race.field.cars[i].frame.intro_ms = intro;
		race.field.cars[i].frame.time.now_ms =
			(float)((long double)seconds * 1000.0L);
		race.field.cars[i].frame.time.go_ms = go;
		race.field.cars[i].seconds = seconds - .001;
	}
}

static void
marker_at(unsigned int entry, struct mh_scene_key *keys, unsigned int key)
{
	unsigned int i;
	struct mh_drive *drive;

	drive = &race.field.cars[entry];
	drive->race.parameter = keys[key].time - 1;

	for (i = 0; i < 3; i++)
		drive->vehicle.motion.position[i] = keys[key].value[i] -
			.319999992847442626953125 *
				drive->vehicle.motion.rotation[3 + i];

	memset(drive->vehicle.motion.velocity, 0,
		sizeof(drive->vehicle.motion.velocity));
}

/*
 * Controlled packet arrivals belong to ticks, including repeatable loss/delay.
 */
static void
fixed_network_schedules(void)
{
	static double reference[240][128];
	double sample[128], *v, now;
	const unsigned int rates[] = {120, 30, 60, 144, 260, 360, 0};
	struct mh_tick_clock clock;
	struct mh_engine_input controls;
	struct mh_net_vehicle vehicle;
	struct mh_error error;
	unsigned char body[30];
	unsigned int schedule, frame, due, i, j, car, steps;
	int accepted;
	long double elapsed;
	enum mh_error_code status;

	for (schedule = 0; schedule < sizeof(rates) / sizeof(rates[0]);
		schedule++) {
		race = grid_race;
		session = grid_session;
		session.roster.mode = 0;
		race.started = 1;
		clock_at(20, 1000, 10000);
		CHECK(mh_field_simulation_rate(&race.field, 120, NULL) ==
			MH_ERROR_NONE);
		mh_tick_clock_init(&clock, 120, NULL);
		memset(&controls, 0, sizeof(controls));
		memset(&vehicle, 0, sizeof(vehicle));
		frame = 0;

		while (clock.ticks < 240 && failures == 0) {
			elapsed = rates[schedule]
				? 1.0L / rates[schedule]
				: (frame % 17 == 0 ? .4L : .003L);

			if (elapsed > 2 - clock.total / 120)
				elapsed = 2 - clock.total / 120;

			if (elapsed < 0)
				elapsed = 0;

			CHECK(mh_tick_clock_add(&clock, elapsed, 0, NULL) ==
				MH_ERROR_NONE);
			due = mh_tick_clock_due(&clock);

			for (i = 0; i < due && clock.ticks < 240; i++) {
				now = 1000 + (clock.ticks + 1) * (1000.0 / 120);

				/*
				 * Every fourth snapshot is lost; others arrive
				 * two ticks late.
				 */
				if (clock.ticks >= 2 && clock.ticks % 4 == 2 &&
					clock.ticks % 16 != 14) {
					vehicle.position[0] =
						10 + clock.ticks * .01;
					vehicle.position[1] = 1;
					vehicle.position[2] = 3;
					CHECK(mh_net_vehicle_encode(&vehicle,
						body, sizeof(body),
						NULL) == MH_ERROR_NONE);

					for (car = 1; car < race.field.count;
						car++)
						CHECK(mh_net_history_receive(
							&session.replication
								.history[race.layout
										.player[car]],
							now - 2 * (1000.0 / 120),
							body,
							sizeof(body), now,
							&accepted,
							NULL) ==
							MH_ERROR_NONE);
				}

				controls.throttle = clock.ticks < 180;
				controls.brake = clock.ticks >= 180;
				status = mh_net_race_frame(&race, &session,
					&controls, 1.0L / 120, now, 0, &steps,
					&error);

				if (status != MH_ERROR_NONE)
					fprintf(stderr,
						"fixed network tick %lu: %s\n",
						clock.ticks, error.message);

				CHECK(status == MH_ERROR_NONE && steps == 1);
				memset(sample, 0, sizeof(sample));
				v = sample;
				*v++ = race.field.active_mask;
				*v++ = race.field.unavailable_mask;
				*v++ = race.field.ai_random;

				for (car = 0; car < race.field.count; car++) {
					for (j = 0; j < 3; j++) {
						*v++ = race.field.cars[car]
							.vehicle.motion
							.position[j];
						*v++ = race.field.cars[car]
							.vehicle.motion
							.velocity[j];
					}

					for (j = 0; j < 9; j++)
						*v++ = race.field.cars[car]
							.vehicle.motion
							.rotation[j];

					*v++ = race.field.cars[car]
						.vehicle.engine.rpm;
					*v++ = race.field.cars[car]
						.vehicle.engine.gear;
					*v++ = race.field.cars[car].race.now_ms;
					*v++ = race.field.cars[car]
						.race.covered;
					*v++ = race.field.contact_count[car];
					*v++ = race.field.contact_impulse[car];
				}

				if (schedule == 0)
					memcpy(reference[clock.ticks], sample,
						sizeof(sample));
				else
					CHECK(memcmp(reference[clock.ticks],
						sample,
						sizeof(sample)) == 0);
				mh_tick_clock_commit(&clock, NULL);
			}

			frame++;
		}
	}

	race = grid_race;
	session = grid_session;
}

static void
network_contact_selection(void)
{
	static struct mh_net_race initial;
	struct mh_engine_input controls;
	double position[3] = {0, 10, 0};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	unsigned int i, steps;
	unsigned long remote_ticks;

	race = grid_race;
	session = grid_session;
	race.started = 1;
	race.field.active_mask = 3;
	session.roster.players[5].flags = 0;
	session.host = 0;
	session.roster.mode = 0;
	session.replication.status_revision = 1;
	clock_at(20, 1000, 10000);
	CHECK(mh_motion_reset(position, rotation,
		&race.field.cars[1].vehicle.motion,
		NULL) == MH_ERROR_NONE);
	position[0] = 1.5;
	CHECK(mh_motion_reset(position, rotation,
		&race.field.cars[0].vehicle.motion,
		NULL) == MH_ERROR_NONE);
	race.field.cars[0].vehicle.motion.velocity[0] = -10;
	memset(&controls, 0, sizeof(controls));
	initial = race;
	session.replication.status[2].flags = 8;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 100, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 2 && race.field.physics_mask == 3);
	CHECK(race.field.contact_count[0] > 0 &&
		race.field.contact_count[1] > 0);
	CHECK(memcmp(&race.field.cars[1].vehicle.motion,
		&initial.field.cars[1].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);

	for (i = 0; i < 2; i++)
		race.field.cars[i].vehicle.motion =
			initial.field.cars[i].vehicle.motion;

	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 110, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 1 && race.field.contact_count[0] == 0);
	session.replication.status[2].flags = 0;

	for (i = 0; i < 2; i++)
		race.field.cars[i].vehicle.motion =
			initial.field.cars[i].vehicle.motion;

	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 120, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 0 &&
		race.field.physics_mask == 1 &&
		race.field.contact_count[0] == 0);

	for (i = 0; i < 2; i++)
		race.field.cars[i].vehicle.motion =
			initial.field.cars[i].vehicle.motion;

	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 130, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 3 && race.field.contact_count[0] > 0);

	for (i = 0; i < 2; i++)
		race.field.cars[i].vehicle.motion =
			initial.field.cars[i].vehicle.motion;

	remote_ticks = race.field.cars[1].ticks;
	session.roster.players[2].flags = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 140, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.active_mask == 1 && race.field.physics_mask == 3 &&
		race.field.contact_count[0] > 0);
	CHECK(race.field.cars[1].ticks == remote_ticks);

	for (i = 0; i < 2; i++)
		race.field.cars[i].vehicle.motion =
			initial.field.cars[i].vehicle.motion;

	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 150, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 1 && race.field.contact_count[0] == 0);

	/*
	 * Frame body reset follows the retained list too. An unavailable car
	 * keeps its old accumulator until it actually re-enters physics.
	 */
	race.field.unavailable_mask = 1;
	session.replication.status[0].flags = 8;
	CHECK(mh_body_effect_contact(&race.field.cars[0].body_effects, 0,
		NULL) == MH_ERROR_NONE);
	race.field.cars[0].vehicle.grip.tyre_effects.id[0] = 2;
	race.field.cars[0].body_sparks.id[0] = 1;
	saved_race = race;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 160, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 0 &&
		memcmp(&race.field.cars[0].body_effects,
			&saved_race.field.cars[0].body_effects,
			sizeof(struct mh_body_effects)) == 0);
	CHECK(race.field.cars[0].vehicle.grip.tyre_effects.id[0] == 2 &&
		race.field.cars[0].body_sparks.id[0] == 1);
	session.replication.status[0].flags = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 170, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 0 &&
		race.field.unavailable_mask == 0 &&
		race.field.cars[0].body_effects.contact);
	CHECK(race.field.cars[0].vehicle.grip.tyre_effects.id[0] == 2 &&
		race.field.cars[0].body_sparks.id[0] == 1);
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 180, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.physics_mask == 1 &&
		!race.field.cars[0].body_effects.contact);
	CHECK(race.field.cars[0].vehicle.grip.tyre_effects.id[0] == 0 &&
		race.field.cars[0].body_sparks.id[0] == 0);
	race = grid_race;
	session = grid_session;
}

static void
patched_availability(void)
{
	struct mh_engine_input controls;
	unsigned int steps;

	race = grid_race;
	session = grid_session;
	session.config.wire = MH_NET_WIRE_31_LOBBY;
	session.roster.mode = 0;
	memset(&controls, 0, sizeof(controls));
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 8000, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 8000.125, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 6);
	session.replication.history[2].received_ms = 8000;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 8001, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 4);

	/* Client silence disables every racing car, including its own. */
	session.host = 0;
	session.replication.local_valid = 0;
	session.replication.history[2].received_ms = 1;
	session.replication.status_revision = 0;
	CHECK(mh_net_session_tick(&session, 8001.25, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 8001.25, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 7 &&
		!race.field.cars[0].race_frozen);

	race = grid_race;
	session = grid_session;
}

static void
network_timer_boundaries(void)
{
	struct mh_engine_input controls;
	unsigned int mode, i, steps;
	unsigned long ticks;

	memset(&controls, 0, sizeof(controls));

	/*
	 * The clock returns 100001.999... at this binary64 seconds sample.
	 * Rounding milliseconds first incorrectly stores 100002.
	 */
	race = grid_race;
	session = grid_session;
	session.roster.mode = 0;
	race.started = 1;
	clock_at(100.002, 1000, 10000);
	race.field.cars[0].race.finished = 1;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.finish.anchor_ms == 100001 && !race.ended);

	for (mode = 0; mode < 3; mode++) {
		race = grid_race;
		session = grid_session;
		session.roster.mode = mode == 2 ? 1 : 0;
		race.started = 1;
		clock_at(100.001, 1000, 10000);

		for (i = 0; i < race.field.count; i++)
			race.field.cars[i].race.finished = mode != 0 || i == 0;

		CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0,
			&steps, NULL) == MH_ERROR_NONE);
		CHECK(race.finish.anchor_ms ==
				(mode == 0 ? 100001UL : 75001UL) &&
			!race.ended);
		clock_at(mode == 0 ? 135.001 : 110.001, 1000, 10000);
		ticks = race.field.cars[0].ticks;
		CHECK(mh_net_race_frame(&race, &session, &controls, 0, 101, 0,
			&steps, NULL) == MH_ERROR_NONE);
		CHECK(race.ended && steps == 0 &&
			session.phase == MH_NET_LOBBY);
		CHECK(race.field.cars[0].ticks == ticks);

		for (i = 0; i < race.field.count; i++)
			CHECK(session.roster.players[race.layout.player[i]]
					.values[4] == 1);
	}

	race = grid_race;
	session = grid_session;
	race.started = 1;
	race.elimination.eliminated = (1U << 2) | (1U << 5);
	clock_at(100.001, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.elimination.anchor_ms == 100001 && race.winner == 0 &&
		!race.ended);
	clock_at(105.001, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 101, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.ended && steps == 1 && session.phase == MH_NET_LOBBY);

	for (i = 0; i < race.field.count; i++)
		CHECK(session.roster.players[race.layout.player[i]].values[4] ==
			1);

	race = grid_race;
	session = grid_session;
}

static void
patched_finish_progress(void)
{
	struct mh_engine_input controls;
	unsigned int mode, i, player, steps;

	memset(&controls, 0, sizeof(controls));

	for (mode = 0; mode <= MH_NET_MODE_ELIMINATION; mode++) {
		race = grid_race;
		session = grid_session;
		session.config.wire = MH_NET_WIRE_31_LOBBY;
		session.config.mode = session.roster.mode = mode;
		race.started = 1;
		race.finish.anchor_ms = 75000;
		race.finish.all_finished = 1;
		clock_at(110.001, 1000, 10000);

		for (i = 0; i < race.field.count; i++) {
			player = race.layout.player[i];
			race.field.cars[i].race.finished = 1;
			race.field.cars[i].race.lap = 2 + i;
			CHECK(mh_net_session_status(&session, player, 1, 1,
				NULL) == MH_ERROR_NONE);
		}

		session.out_count = MH_NET_OUTBOX - 1;
		saved_session = session;
		saved_race = race;
		CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0,
			&steps, NULL) == MH_ERROR_RANGE);
		CHECK(!memcmp(&session, &saved_session, sizeof(session)));
		CHECK(!memcmp(&race, &saved_race, sizeof(race)));
		session.out_count = 0;
		CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0,
			&steps, NULL) == MH_ERROR_NONE);
		CHECK(race.ended && session.phase == MH_NET_LOBBY);

		for (i = 0; i < race.field.count; i++) {
			player = race.layout.player[i];
			CHECK(session.roster.players[player].value_a ==
				race.field.standings.cars[i].position);
			CHECK(session.roster.players[player].value_b == 2 + i);
		}
	}

	race = grid_race;
	session = grid_session;
}

static void
eliminated_finishers(void)
{
	struct mh_engine_input controls;
	unsigned int steps, i;

	memset(&controls, 0, sizeof(controls));
	race = grid_race;
	session = grid_session;
	race.started = 1;
	clock_at(100, 1000, 10000);

	/* Field entry 2 is wire slot 5; it remains active and unfinished. */
	race.elimination.eliminated = 1U << 5;
	race.field.cars[2].race_frozen = 1;

	for (i = 0; i < 2; i++) {
		race.field.cars[i].race.finished = 1;
		race.field.cars[i].race.now_ms = 40000 + i * 1000;
	}

	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.finish.all_finished && race.finish.anchor_ms == 75000);
	CHECK(race.field.active_mask == 7 && !race.field.cars[2].race.finished);
	CHECK(race.elimination.anchor_ms == 0 && race.winner == MH_NET_PLAYERS);
	clock_at(110, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 101, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		!race.ended);
	CHECK(mh_net_race_frame(&race, &session, &controls, .000125, 102, 0,
		&steps, NULL) == MH_ERROR_NONE &&
		race.ended);
	CHECK(session.phase == MH_NET_LOBBY &&
		!race.field.cars[2].race.finished);

	for (i = 0; i < race.field.count; i++)
		CHECK(session.roster.players[race.layout.player[i]].values[4] ==
			1);

	race = grid_race;
	session = grid_session;
}

/* The dedicated setup receiver remains guarded; exercise a frozen field. */
static void
dedicated_field(const struct mh_car *car, const struct mh_collision *hull,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_route *route, const struct mh_track *track)
{
	static struct mh_net_session client;
	static struct mh_net_race driving;
	struct mh_net_session_config config;
	struct mh_error error;
	const struct mh_car *cars[MH_NET_WIRE_PLAYERS - 1];
	struct mh_collision hulls[MH_NET_WIRE_PLAYERS - 1];
	struct mh_engine_input controls;
	struct mh_net_race_layout layout;
	unsigned int i, mode, steps;

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = track->laps;
	config.car_count = 10;
	config.profile.update_rate = 15;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.car, "Kobra X3");
	strcpy(config.profile.name, "Driver");
	memset(&controls, 0, sizeof(controls));

	for (i = 0; i < MH_NET_WIRE_PLAYERS - 1; i++) {
		cars[i] = car;
		hulls[i] = *hull;
	}

	for (mode = 0; mode <= MH_NET_MODE_ELIMINATION; mode++) {
		config.mode = mode;
		CHECK(mh_net_session_init(0, &config, &client, NULL) ==
			MH_ERROR_NONE);
		client.phase = MH_NET_LOADING;
		client.player = 12;
		client.roster.player_count = MH_NET_WIRE_PLAYERS;
		client.roster.laps = track->laps;
		client.roster.mode = mode;
		client.roster.players[0].flags = 0;

		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
			client.roster.players[i] = config.profile;
			client.roster.players[i].flags = MH_NET_FLAG_RACING;
			client.roster.players[i].value_a = i;
		}

		if (mh_net_race_start(&client, cars, hulls, col, materials,
			route, track, &driving, &error) != MH_ERROR_NONE) {
			fprintf(stderr, "dedicated field: %s\n", error.message);
			failures++;
			return;
		}

		CHECK(driving.field.count == 12 &&
			driving.layout.entry[0] == MH_NET_WIRE_PLAYERS &&
			driving.layout.player[0] == 12);

		for (i = 0; i < 12; i++) {
			CHECK(driving.field.cars[i].slot ==
				driving.layout.player[i] - 1);
			CHECK(driving.field.cars[i].remote == (i != 0));
		}

		CHECK(mh_net_race_frame(&driving, &client, &controls, .01, 10,
			11, &steps, NULL) == MH_ERROR_NONE);
		CHECK(client.replication.view[12] == 11);
		client.roster.players[11].flags = 0;
		CHECK(mh_net_race_frame(&driving, &client, &controls, .01, 20,
			11, &steps, NULL) == MH_ERROR_NONE);
		CHECK(driving.field.active_mask == 0x7ff &&
			driving.field.viewed_entry == 0);
		CHECK(mh_net_race_return(&driving, &client, NULL) ==
				MH_ERROR_NONE &&
			client.phase == MH_NET_LOBBY);

		/*
		 * Raising the wire storage must not raise listen-host limits.
		 */
		client.phase = MH_NET_LOADING;
		client.host = 1;
		CHECK(mh_net_race_layout(&client, &layout, NULL) !=
			MH_ERROR_NONE);
	}
}

int
main(void)
{
	static const unsigned char defs[] = "Materials 1\nMaterialName road\n";
	static const unsigned char surfaces[] = "Materials 1\nMaterial road\n";
	struct mh_net_session_config config;
	struct mh_net_race_layout layout;
	struct mh_car car;
	const struct mh_car *cars[3];
	struct mh_collision col, hulls[3];
	struct mh_materials materials;
	struct mh_track track;
	struct mh_route route;
	struct mh_scene_key keys[65];
	struct mh_placement pose;
	struct mh_net_vehicle vehicle;
	struct mh_engine_input controls;
	unsigned char empty[62], hull_bytes[278], body[30];
	unsigned int i, slot, steps;
	int accepted;
	double now;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.laps = 1;
	config.car_count = 10;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Local");
	strcpy(config.profile.car, "Kobra X3");
	config.profile.update_rate = 15;
	CHECK(mh_net_session_init(0, &config, &session, NULL) == MH_ERROR_NONE);

	/*
	 * Sparse, already authenticated/loading roster. Wire behavior has
	 * separate session tests.
	 */
	session.phase = MH_NET_LOADING;
	session.player = 2;
	session.roster.player_count = 6;
	session.roster.laps = 1;

	for (i = 0; i < 3; i++) {
		slot = i == 0 ? 0 : (i == 1 ? 2 : 5);
		session.roster.players[slot] = config.profile;
		session.roster.players[slot].flags = 1;
		session.roster.players[slot].value_a = i + 1;
		session.roster.players[slot].gear_mode = i == 1 ? 0 : 1;
	}

	CHECK(mh_net_race_layout(&session, &layout, NULL) == MH_ERROR_NONE &&
		layout.count == 3);
	CHECK(layout.player[0] == 2 && layout.player[1] == 0 &&
		layout.player[2] == 5);
	CHECK(layout.entry[0] == 1 && layout.entry[2] == 0 &&
		layout.entry[5] == 2 && layout.entry[1] == MH_NET_WIRE_PLAYERS);
	session.roster.players[5].value_a = 2;
	CHECK(mh_net_race_layout(&session, &layout, NULL) != MH_ERROR_NONE);
	session.roster.players[5].value_a = 3;
	session.roster.mode = 1;
	CHECK(mh_net_race_layout(&session, &layout, NULL) == MH_ERROR_NONE);
	session.roster.mode = 2;
	CHECK(mh_net_race_layout(&session, &layout, NULL) != MH_ERROR_NONE);
	session.roster.mode = 0;
	memset(empty, 0, sizeof(empty));
	empty[0] = 42;
	empty[4] = empty[6] = empty[8] = empty[12] = empty[14] = 1;
	empty[34] = empty[38] = 0x80;
	empty[35] = empty[39] = 0x3f;
	CHECK(mh_collision_col_decode(empty, sizeof(empty), &col, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_collision_col_decode(hull_bytes, pair_fixture(hull_bytes),
		&hulls[0], NULL) == MH_ERROR_NONE);
	hulls[1] = hulls[2] = hulls[0];
	CHECK(mh_materials_mat_srf_decode(defs, sizeof(defs) - 1, surfaces,
		sizeof(surfaces) - 1, &materials, NULL) == MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
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

	for (i = 0; i < 3; i++)
		cars[i] = &car;

	memset(&track, 0, sizeof(track));
	track.has_start_grid = 1;
	track.laps = 1;
	track.view_distance = 180;
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
	dedicated_field(&car, &hulls[0], &col, &materials, &route, &track);
	saved_session = session;
	memset(&race, 0xa5, sizeof(race));
	saved_race = race;
	track.view_distance = 0;
	CHECK(mh_net_race_start(&session, cars, hulls, &col, &materials, &route,
		&track, &race, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&session, &saved_session, sizeof(session)) == 0);
	track.view_distance = 180;
	CHECK(mh_net_race_start(&session, cars, hulls, &col, &materials, &route,
		&track, &race, NULL) == MH_ERROR_NONE);
	CHECK(session.phase == MH_NET_WAITING &&
		session.replication.local_valid && race.field.network);
	CHECK(race.field.cars[0].config.options.manual &&
		!race.field.cars[1].config.options.manual);
	CHECK(race.field.cars[0].slot == 1 && race.field.cars[1].slot == 0 &&
		race.field.cars[2].slot == 2);

	for (i = 0; i < race.field.count; i++) {
		CHECK(mh_placement_grid(&track, &route, race.field.cars[i].slot,
			&pose, NULL) == MH_ERROR_NONE);
		CHECK(fabs(race.samples[i].replica.vehicle.position[0] -
			pose.position[0]) < .0001);
		CHECK(fabs(race.samples[i].replica.vehicle.position[2] -
			pose.position[2]) < .0001);
	}
	{
		static struct mh_field crossing;
		struct mh_records records;

		memset(&records, 0, sizeof(records));
		CHECK(mh_field_bind_records(&race.field, &records, 7, NULL) !=
			MH_ERROR_NONE);
		CHECK(!race.field.records_bound);
		CHECK(mh_field_bind_records(&race.field, &records, 1, NULL) ==
			MH_ERROR_NONE);
		crossing = race.field;
		crossing.active_mask = 1;
		crossing.cars[0].countdown = 0;
		crossing.cars[0].vehicle.motion.position[0] = 40;
		crossing.cars[0].vehicle.motion.position[2] = 1;
		crossing.cars[0].race.previous_position[0] = 40;
		crossing.cars[0].race.previous_position[2] = -1;
		crossing.cars[0].race.group = 3;
		crossing.cars[0].race.covered = 4;
		memset(crossing.cars[0].race.visited, 1, 4);
		memset(&controls, 0, sizeof(controls));
		CHECK(mh_field_tick(&crossing, &controls, NULL) ==
			MH_ERROR_NONE);
		CHECK(crossing.records.has_lap && crossing.records.has_race);
		CHECK(crossing.cars[0].race.lap_notice == 2 &&
			crossing.cars[0].race.race_notice == 2);
		CHECK(!crossing.cars[1].race.record_eligible &&
			!crossing.cars[2].race.record_eligible);
	}

	CHECK(mh_placement_grid(&track, &route, 1, &pose, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(session.replication.local.position[0] - pose.position[0]) <
		.0001);
	memset(&controls, 0, sizeof(controls));
	controls.throttle = .5;
	saved_race = race;

	for (i = 1; i <= 10; i++) {
		now = i * 10;
		CHECK(mh_net_race_frame(&race, &session, &controls, .01, now, 0,
			&steps, NULL) == MH_ERROR_NONE);
	}

	CHECK(race.field.cars[0].ticks == 10 &&
		race.field.cars[0].start_ticks == 0 &&
		race.field.cars[0].race.now_ms == 0);

	/* Warmup never integrates a remote. */
	CHECK(memcmp(&race.field.cars[1].vehicle.motion,
		&saved_race.field.cars[1].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);
	vehicle = session.replication.local;
	vehicle.position[0] += 100;

	for (i = 1; i <= 10; i++) {
		vehicle.position[2] += .1;
		CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_net_history_receive(&session.replication.history[0],
			i * 10, body, 30, 100, &accepted,
			NULL) == MH_ERROR_NONE &&
			accepted);
	}

	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 100, 1, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(race.samples[1].replica.vehicle.position[0] ==
		(double)(float)vehicle.position[0]);
	CHECK(session.replication.view[2] == 0 && race.rejected_mask == 0);
	CHECK(race.field.viewed_entry == 1);
	CHECK(session.replication.local.position[0] !=
		race.samples[1].replica.vehicle.position[0]);
	session.start_pending = 1;
	CHECK(mh_net_race_frame(&race, &session, &controls, .25, 100, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 7 && race.started);
	CHECK(race.start_tick == 11 &&
		race.field.cars[0].frame.intro_ms == 350);
	CHECK(race.field.cars[0].frame.time.go_ms == 0 &&
		race.field.cars[0].race.now_ms == 0);
	clock_at(16, 4000, 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 109, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(race.field.cars[0].frame.time.go_ms == 0 &&
		race.field.cars[0].race.now_ms == 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, .0005, 110, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.field.cars[0].frame.time.go_ms == 16000 &&
		race.field.cars[0].race.now_ms == 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, .0085, 118, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.field.cars[0].race.now_ms == 9 && race.start_tick == 11);
	saved_race = race;
	saved_session = session;
	steps = 99;
	controls.shift_up = 2;
	CHECK(mh_net_race_frame(&race, &session, &controls, .1, 218, 0, &steps,
		NULL) != MH_ERROR_NONE &&
		steps == 99);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&session, &saved_session, sizeof(session)) == 0);
	controls.shift_up = 0;
	session.roster.players[5].flags = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 128, 2, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.active_mask == 3 && session.replication.view[2] == 2);
	CHECK(race.field.viewed_entry == 0);
	CHECK(race.field.cars[2].ticks == saved_race.field.cars[2].ticks);

	/*
	 * A valid packet can imply excessive extrapolated motion. Keep that
	 * car's last pose without stopping local controls or moving the view
	 * cache.
	 */
	saved_race = race;
	mh_net_history_reset(&session.replication.history[0]);

	for (i = 1; i <= 10; i++) {
		vehicle.position[0] = i % 2 ? 100000 : -100000;
		CHECK(mh_net_vehicle_encode(&vehicle, body, 30, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_net_history_receive(&session.replication.history[0],
			1000 + i * 10, body, 30, 130, &accepted,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 130, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.rejected_mask == 2 && steps == 1);
	CHECK(memcmp(&race.field.cars[1].vehicle.motion,
		&saved_race.field.cars[1].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0);
	CHECK(mh_motion_display(&race.field.cars[1].vehicle.motion,
		vehicle.position, vehicle.angles, NULL) == MH_ERROR_NONE);
	CHECK(session.replication.position[0][0] == vehicle.position[0]);
	CHECK(mh_field_reset(&race.field, 1, NULL) != MH_ERROR_NONE);
	race.field.cars[1].race.finished = 1;
	race.field.cars[1].race.now_ms = 1234;
	CHECK(mh_field_set_active(&race.field, 1, NULL) == MH_ERROR_NONE);
	CHECK(race.field.standings.cars[0].position == 1 &&
		race.field.standings.cars[1].position == 0);
	CHECK(race.field.standings.cars[1].finished &&
		race.field.standings.cars[1].finish_ms == 1234);
	CHECK(race.field.standings.finished_count == 0 &&
		!race.field.standings.complete);
	saved_race = race;
	CHECK(mh_field_set_active(&race.field, 3, NULL) != MH_ERROR_NONE &&
		mh_field_set_active(&race.field, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&race, &saved_race, sizeof(race)) == 0);

	/*
	 * Host mode 1, sparse wire slots. Late checkpoint acceptance, frozen
	 * host progress, physics exclusion, bounded notices and scored return.
	 */
	config.mode = 1;
	CHECK(mh_net_session_init(1, &config, &session, NULL) == MH_ERROR_NONE);
	session.phase = MH_NET_LOADING;
	session.roster.mode = 1;
	session.roster.player_count = 6;
	session.roster.laps = 1;

	for (i = 0; i < 3; i++) {
		slot = i == 0 ? 0 : (i == 1 ? 2 : 5);
		session.roster.players[slot] = config.profile;
		session.roster.players[slot].flags = 1;
		session.roster.players[slot].value_a = i + 1;
		sprintf(session.roster.players[slot].name, "Driver%u", slot);
		session.lobby.slots[slot].profile =
			session.roster.players[slot];
		session.lobby.slots[slot].phase = MH_NET_IN_RACE;
		session.lobby.slots[slot].peer = slot + 2;
	}

	CHECK(mh_net_race_start(&session, cars, hulls, &col, &materials, &route,
		&track, &race, NULL) == MH_ERROR_NONE);
	session.phase = MH_NET_RACING;
	session.start.started_ms = 0;
	grid_race = race;
	grid_session = session;

	fixed_network_schedules();
	network_contact_selection();
	patched_availability();
	network_timer_boundaries();
	patched_finish_progress();
	eliminated_finishers();
	unavailable_contacts(&race.field);

	/*
	 * Availability is independent of elimination. Stale remote snapshots
	 * recover; a client's own flag comes only from received host status.
	 */
	session.roster.mode = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1000, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1000.125, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 6);
	session.replication.history[2].received_ms = 1000.125;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1001, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 4);
	session.host = 0;
	session.replication.status_revision = 1;
	session.replication.status[0].flags = 8;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1002, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 1 &&
		!race.field.cars[0].race_frozen);
	session.replication.status[0].flags = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1003, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.unavailable_mask == 0);
	race = grid_race;
	session = grid_session;

	/*
	 * Frame-end progress sees the remote's applied position once, after
	 * all seven physical slices; its checkpoint pulse reaches elimination.
	 */
	race.started = 1;
	clock_at(20, 1000, 10000);
	keys[16].value[4] = 1;
	marker_at(1, keys, 16);
	CHECK(mh_net_race_frame(&race, &session, &controls, .25, 250, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		steps == 7);
	CHECK(race.elimination.cursor[2] == 1 &&
		race.field.cars[1].race.split_event == 2);
	CHECK(race.field.cars[1].race.splits[1][2] == 10250);
	race = grid_race;
	session = grid_session;
	race.started = 1;
	clock_at(20, 1000, 10000);
	marker_at(0, keys, 16);
	race.field.standings.cars[0].position = 1;
	race.field.standings.cars[1].position = 2;
	saved_race = race;
	session.out_count = MH_NET_OUTBOX;
	saved_session = session;
	steps = 99;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 50000, 0, &steps,
		NULL) != MH_ERROR_NONE);
	CHECK(steps == 99 && memcmp(&race, &saved_race, sizeof(race)) == 0 &&
		memcmp(&session, &saved_session, sizeof(session)) == 0);
	session.out_count = 0;
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 50000, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.elimination.cursor[0] == 1 &&
		race.elimination.checkpoints[2][0].deadline_ms == 13000);
	CHECK(race.elimination.checkpoints[5][0].deadline_ms == 13000 &&
		session.out_count == 2);
	race.field.standings.cars[0].position = 1;
	race.field.standings.cars[1].position = 2;
	clock_at(24, 1000, 10000);
	marker_at(1, keys, 16);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 50001, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.elimination.eliminated == 32 &&
		race.field.cars[2].race_frozen);
	CHECK(race.elimination.cursor[2] == 1 && race.field.active_mask == 7 &&
		!race.field.cars[2].race.finished);
	CHECK(session.replication.eliminated == 32 && race.winner == 8);
	race.field.cars[2].race.sound_blend = .625f;
	saved_race = race;
	CHECK(mh_net_race_frame(&race, &session, &controls, .25, 50251, 0,
		&steps, NULL) == MH_ERROR_NONE &&
		steps == 7);
	CHECK(memcmp(&race.field.cars[2].race, &saved_race.field.cars[2].race,
		sizeof(struct mh_race)) == 0);
	CHECK(race.field.cars[2].ticks == saved_race.field.cars[2].ticks + 7);
	race.field.standings.cars[1].position = 1;
	race.field.standings.cars[0].position = 2;
	keys[32].value[4] = 2;
	clock_at(30, 1000, 10000);
	marker_at(1, keys, 32);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 51000, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.elimination.checkpoints[0][1].deadline_ms == 23000);
	clock_at(34, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 51001, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.cars[0].race_frozen && race.winner == 2 &&
		race.elimination.anchor_ms == 34000);
	CHECK(!(race.field.unavailable_mask & 1));
	CHECK(strcmp(session.notice.text,
		"You have been eliminated by Driver2") == 0);
	saved_race = race;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 51011, 0,
		&steps, NULL) == MH_ERROR_NONE &&
		steps == 1);
	CHECK(memcmp(&race.field.cars[0].race, &saved_race.field.cars[0].race,
		sizeof(struct mh_race)) == 0);
	CHECK(race.field.cars[0].ticks == saved_race.field.cars[0].ticks + 1);
	CHECK(race.field.unavailable_mask & 1);
	CHECK(race.field.physics_mask & 1);
	CHECK(memcmp(&race.field.cars[0].vehicle.motion,
		&saved_race.field.cars[0].vehicle.motion,
		sizeof(struct mh_motion_state)) != 0);
	saved_race = race;
	CHECK(mh_net_race_frame(&race, &session, &controls, .01, 51021, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(!(race.field.physics_mask & 1));
	CHECK(memcmp(&race.field.cars[0].vehicle.motion,
		&saved_race.field.cars[0].vehicle.motion,
		sizeof(struct mh_motion_state)) == 0 &&
		race.field.contact_count[0] == 0);
	CHECK(mh_drive_reset(&race.field.cars[0], 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&race.field.cars[0].race, &saved_race.field.cars[0].race,
		sizeof(struct mh_race)) == 0);
	clock_at(39, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 56001, 0, &steps,
		NULL) == MH_ERROR_NONE &&
		!race.ended);
	CHECK(mh_net_race_frame(&race, &session, &controls, .000125, 56001.125,
		0, &steps, NULL) == MH_ERROR_NONE &&
		race.ended);
	CHECK(session.phase == MH_NET_LOBBY &&
		session.replication.eliminated == 0);
	CHECK(session.roster.players[0].values[4] == 1 &&
		session.roster.players[2].values[4] == 1 &&
		session.roster.players[5].values[4] == 1);

	/*
	 * A finish detected by this frame's progress starts the host deadline
	 * on the NEXT frame. Transport timestamps cannot advance that deadline.
	 */
	race = grid_race;
	session = grid_session;
	session.roster.mode = 0;
	race.started = 1;
	clock_at(20, 1000, 10000);
	race.field.cars[0].vehicle.motion.position[0] = 40;
	race.field.cars[0].vehicle.motion.position[2] = 1;
	race.field.cars[0].race.previous_position[0] = 40;
	race.field.cars[0].race.previous_position[2] = -1;
	race.field.cars[0].race.group = 3;
	race.field.cars[0].race.covered = 4;
	memset(race.field.cars[0].race.visited, 1, 4);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1000, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.field.cars[0].race.finished && race.finish.anchor_ms == 0);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 1001, 0, &steps,
		NULL) == MH_ERROR_NONE);
	CHECK(race.finish.anchor_ms == 20000);
	clock_at(55, 1000, 10000);
	CHECK(mh_net_race_frame(&race, &session, &controls, 0, 200000, 0,
		&steps, NULL) == MH_ERROR_NONE &&
		!race.ended);
	saved_race = race;
	race.field.cars[2].wheels.angle[0] = 1e7;
	steps = 99;
	CHECK(mh_net_race_frame(&race, &session, &controls, .000125, 200001, 0,
		&steps, NULL) == MH_ERROR_NONE);
	CHECK(race.ended && steps == 0 &&
		race.field.cars[0].ticks == saved_race.field.cars[0].ticks);

	mh_collision_free(&col);
	mh_collision_free(&hulls[0]);

	return failures != 0;
}
