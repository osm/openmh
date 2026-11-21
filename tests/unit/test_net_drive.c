#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/error.h"
#include "engine/core/pose.h"
#include "engine/network/drive.h"
#include "engine/render/scene_matrices.h"
#include "engine/simulation/drive_internal.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)
#define NEAR(a, b)                                                             \
	do {                                                                   \
		if (fabs((a) - (b)) >= .00001) {                               \
			fprintf(stderr, "line %d: %.17g != %.17g\n", __LINE__, \
				(double)(a), (double)(b));                     \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
put32(unsigned char *p, unsigned long x)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(x >> (8 * i));
}

/* One broad triangular floor in a single 1000-unit cell. */
static void
floor_fixture(unsigned char data[142])
{
	unsigned int i;

	memset(data, 0, 142);
	data[0] = 42;
	data[4] = data[6] = data[8] = data[10] = data[12] = 1;
	data[14] = 2;
	data[18] = 3;
	data[40] = 1;
	data[41] = 0x80;

	for (i = 0; i < 3; i++)
		put32(data + 20 + 4 * i, 0x43fa0000UL);

	put32(data + 32, 0x3a83126fUL);
	put32(data + 36, 0x447a0000UL);
	data[62] = 1;
	data[66] = 7;
	data[68] = 1;
	data[70] = 2;
	data[72] = 3;
	put32(data + 82, 0x3f800000UL);
	put32(data + 94, 0x3f800000UL);
	put32(data + 118, 0x3f800000UL);
	put32(data + 126, 0xbf3504f3UL);
	put32(data + 134, 0xbf3504f3UL);

	for (i = 0; i < 3; i++)
		put32(data + 106 + 16 * i, 0x42c80000UL);
}

int
main(void)
{
	struct mh_drive local, remote, saved;
	struct mh_net_drive_sample sample, old_sample;
	struct mh_net_drive_effects effects;
	struct mh_net_vehicle vehicle, old_vehicle;
	struct mh_net_playback playback;
	struct mh_net_history history;
	struct mh_engine_input controls;
	struct mh_collision col;
	struct mh_car car;
	struct mh_track track;
	struct mh_materials materials;
	unsigned char empty[62], bytes[30], floor_data[142];
	double position[3], velocity[3], angles[3];
	float models[MH_DRIVE_MESHES * 16], matrix[16], view[16];
	unsigned int i;
	int accepted;

	memset(empty, 0, sizeof(empty));
	empty[0] = 42;
	empty[4] = empty[6] = empty[8] = empty[12] = empty[14] = 1;
	empty[34] = empty[38] = 0x80;
	empty[35] = empty[39] = 0x3f;
	CHECK(mh_collision_col_decode(empty, sizeof(empty), &col, NULL) ==
		MH_ERROR_NONE);
	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	car.spring_length = .5;
	memset(&local, 0, sizeof(local));
	memset(&controls, 0, sizeof(controls));
	local.car = &car;
	local.col = &col;
	local.track = &track;
	local.suspension.length = car.spring_length;
	local.suspension.damping = 65000;
	memset(&materials, 0, sizeof(materials));
	materials.count = 1;
	materials.entries[0].spark_anim = 1;
	local.materials = &materials;
	position[0] = 4;
	position[1] = 3;
	position[2] = 2;
	angles[0] = angles[2] = 0;
	angles[1] = 1.5707963705062866;
	CHECK(mh_pose_matrix(angles, local.vehicle.motion.rotation, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_motion_reset(position, local.vehicle.motion.rotation,
		&local.vehicle.motion, NULL) == MH_ERROR_NONE);
	local.vehicle.engine.rpm = 4000;
	local.vehicle.engine.gear = 2;
	local.vehicle.wheel_rate[0] = 4;
	local.vehicle.wheel_rate[1] = 20;
	local.vehicle.brake_active = local.vehicle.grip.sliding =
		local.vehicle.wheelspin = 1;
	local.vehicle.heavy_brake = local.vehicle.engine_braking = 1;
	local.sound_pedal = .75;
	local.vehicle.smoothed_steering = .4;
	local.vehicle.grip.slide_amount = 2;

	for (i = 0; i < 4; i++)
		local.vehicle.grip.wheels[i].supported = 1;

	memset(&effects, 0, sizeof(effects));
	local.horn = 1;
	effects.auxiliary[0] = .5;
	effects.codes[0] = 3;
	effects.variants[1] = 2;
	effects.codes[2] = 1;
	effects.auxiliary[2] = .75;
	effects.variants[0] = 3;
	CHECK(mh_net_drive_capture(&local, &effects, &vehicle, NULL) ==
			MH_ERROR_NONE &&
		vehicle.flags == 2047);
	CHECK(vehicle.wheel_speed[0] == 4 && vehicle.wheel_speed[1] == 20 &&
		vehicle.codes[0] == 3 && vehicle.variants[1] == 2);
	CHECK(mh_motion_display(&local.vehicle.motion, position, velocity,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		NEAR(vehicle.position[i], position[i]);

	CHECK(mh_net_drive_capture(&local, NULL, &vehicle, NULL) ==
			MH_ERROR_NONE &&
		(vehicle.flags & 2));
	local.vehicle.grip.tyre_effects.id[0] = 2;
	local.vehicle.grip.tyre_effects.gain[0] = .25;
	CHECK(mh_body_effect_impact(&local.body_effects, 0, 2, 100, 1, 20,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_drive_body_publish(&local, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_drive_capture(&local, NULL, &vehicle, NULL) ==
		MH_ERROR_NONE);
	CHECK(vehicle.codes[0] == 2 && vehicle.auxiliary[0] == .25 &&
		vehicle.codes[2] == 1 && vehicle.variants[0] == 2);
	CHECK(vehicle.auxiliary[2] == (double)(float).6);
	local.horn = 0;
	CHECK(mh_net_drive_capture(&local, NULL, &vehicle, NULL) ==
			MH_ERROR_NONE &&
		!(vehicle.flags & 2));
	local.horn = 2;
	old_vehicle = vehicle;
	CHECK(mh_net_drive_capture(&local, NULL, &vehicle, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&vehicle, &old_vehicle, sizeof(vehicle)) == 0);
	local.horn = 1;

	/*
	 * Reject an invalid final effect without publishing earlier channels.
	 */
	local.body_sparks.id[1] = 2;
	CHECK(mh_net_drive_capture(&local, NULL, &vehicle, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&vehicle, &old_vehicle, sizeof(vehicle)) == 0);
	local.body_sparks.id[1] = 0;
	CHECK(mh_net_drive_capture(&local, &effects, &vehicle, NULL) ==
		MH_ERROR_NONE);
	remote = local;
	remote.sound_pedal = .25;
	mh_net_history_reset(&history);

	for (i = 1; i <= 10; i++) {
		vehicle.position[0] = 4 + i * .2;
		CHECK(mh_net_vehicle_encode(&vehicle, bytes, sizeof(bytes),
			NULL) == MH_ERROR_NONE);
		CHECK(mh_net_history_receive(&history, i * 100, bytes,
			sizeof(bytes), i * 100, &accepted,
			NULL) == MH_ERROR_NONE &&
			accepted);
	}

	CHECK(mh_net_history_sample(&history, 1000, &playback, NULL) ==
			MH_ERROR_NONE &&
		playback.ready && playback.updated);
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) ==
			MH_ERROR_NONE &&
		remote.remote && remote.horn);
	NEAR(sample.replica.velocity[2], 2);

	/* Binary32 position differencing. */
	CHECK(fabs(sample.replica.speed - 7.2) < .00002);
	CHECK(remote.vehicle.speed == (float)sample.replica.speed);
	CHECK(remote.sound_pedal == 1 && remote.vehicle.brake_active &&
		remote.vehicle.wheelspin);
	CHECK(remote.vehicle.heavy_brake && remote.vehicle.engine_braking);
	CHECK(remote.vehicle.grip.tyre_effects.id[0] ==
		playback.vehicle.codes[0]);
	CHECK(remote.vehicle.grip.tyre_effects.gain[0] ==
		playback.vehicle.auxiliary[0]);
	CHECK(remote.body_sparks.id[0] == 1 &&
		remote.body_sparks.gain[0] == playback.vehicle.auxiliary[2]);
	CHECK(remote.body_sparks.corner[0] == 3 &&
		remote.body_sparks.corner[1] == 2);
	CHECK(mh_motion_display(&remote.vehicle.motion, position, velocity,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		NEAR(position[i], playback.vehicle.position[i]);

	for (i = 0; i < 4; i++) {
		CHECK(!sample.contacts[i].hit &&
			sample.contacts[i].normal[1] == 1);
		CHECK(memcmp(&remote.vehicle.wheel_contacts[i],
			&sample.contacts[i],
			sizeof(sample.contacts[i])) == 0);
		CHECK(sample.start[i][1] > sample.end[i][1]);
		NEAR(sample.contacts[i].position[1], sample.end[i][1]);
	}

	/*
	 * A decoded/extrapolated extension over one remains visible, while
	 * local motion bookkeeping stays valid for camera/traffic queries.
	 */
	playback.vehicle.extension[0] = 1.4;
	playback.vehicle.flags = 0;
	floor_fixture(floor_data);
	mh_collision_free(&col);
	CHECK(mh_collision_col_decode(floor_data, sizeof(floor_data), &col,
		NULL) == MH_ERROR_NONE);
	playback.vehicle.position[1] = .4;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) ==
			MH_ERROR_NONE &&
		remote.sound_pedal == 1 && !remote.horn);
	CHECK(sample.contacts[0].hit && sample.contacts[0].material == 7);
	CHECK(memcmp(remote.vehicle.wheel_contacts, sample.contacts,
		sizeof(sample.contacts)) == 0);
	CHECK(remote.vehicle.contact_origin_y ==
		remote.vehicle.motion.position[1]);
	NEAR(sample.contacts[0].position[1], 0);

	/* Network flags remain authoritative. */
	CHECK(!remote.vehicle.grip.wheels[0].supported);
	CHECK(remote.vehicle.motion.extension[0] == 1);
	NEAR(remote.remote_extension[0], 1.4);
	saved = remote;
	CHECK(mh_drive_tick(&remote, &controls, NULL) != MH_ERROR_NONE &&
		memcmp(&remote, &saved, sizeof(remote)) == 0);
	CHECK(mh_drive_reset(&remote, 0, NULL) != MH_ERROR_NONE &&
		memcmp(&remote, &saved, sizeof(remote)) == 0);
	CHECK(mh_drive_remote_tick(&remote, NULL) == MH_ERROR_NONE &&
		remote.ticks == 1);
	CHECK(memcmp(remote.vehicle.motion.position,
		saved.vehicle.motion.position, sizeof(position)) == 0);
	CHECK(remote.wheels.angle[0] != remote.wheels.angle[1]);
	mh_drive_matrices(&remote, 1.5, models, matrix, view);
	NEAR(models[45],
		sample.replica.vehicle.position[1] -
			.5 * remote.remote_extension[0]);
	saved = remote;
	old_sample = sample;

	/* A late contact failure must also roll back prepared remote motion. */
	car.wheel_position[3][2] = 10001;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&remote, &saved, sizeof(remote)) == 0 &&
		memcmp(&sample, &old_sample, sizeof(sample)) == 0);
	car.wheel_position[3][2] = 0;

	/* Replica validation must leave both application outputs untouched. */
	playback.vehicle.codes[3] = 2;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&remote, &saved, sizeof(remote)) == 0 &&
		memcmp(&sample, &old_sample, sizeof(sample)) == 0);
	playback.vehicle.codes[3] = sample.replica.vehicle.codes[3];

	playback.world_velocity[0] = 100000;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&remote, &saved, sizeof(remote)) == 0 &&
		memcmp(&sample, &old_sample, sizeof(sample)) == 0);
	playback.ready = 0;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&remote, &saved, sizeof(remote)) == 0);
	playback.ready = 1;
	playback.updated = 0;
	CHECK(mh_net_drive_apply(&remote, &playback, &sample, NULL) ==
			MH_ERROR_NONE &&
		memcmp(&sample, &old_sample, sizeof(sample)) == 0);
	old_vehicle = vehicle;
	local.vehicle.brake_active = 2;
	CHECK(mh_net_drive_capture(&local, &effects, &vehicle, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&old_vehicle, &vehicle, sizeof(vehicle)) == 0);
	CHECK(mh_net_drive_capture(&remote, NULL, &vehicle, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_drive_capture_remote(&remote, &sample.replica.vehicle,
		&vehicle, NULL) == MH_ERROR_NONE);
	old_vehicle = vehicle;
	saved = remote;
	remote.sound_pedal = -1;
	CHECK(mh_net_drive_capture_remote(&remote, &sample.replica.vehicle,
		&vehicle, NULL) != MH_ERROR_NONE &&
		memcmp(&vehicle, &old_vehicle, sizeof(vehicle)) == 0);
	remote = saved;
	CHECK(mh_net_drive_capture_remote(&local, &sample.replica.vehicle,
		&vehicle, NULL) != MH_ERROR_NONE &&
		memcmp(&vehicle, &old_vehicle, sizeof(vehicle)) == 0);
	old_sample = sample;
	sample.replica.vehicle.position[0] = 1e40;
	CHECK(mh_net_drive_capture_remote(&remote, &sample.replica.vehicle,
		&vehicle, NULL) != MH_ERROR_NONE &&
		memcmp(&vehicle, &old_vehicle, sizeof(vehicle)) == 0);
	sample = old_sample;
	CHECK(mh_net_drive_apply(NULL, &playback, &sample, NULL) !=
			MH_ERROR_NONE &&
		mh_net_drive_apply(&remote, NULL, &sample, NULL) !=
			MH_ERROR_NONE);
	CHECK(mh_net_drive_apply(&remote, &playback, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_drive_capture(NULL, NULL, &vehicle, NULL) !=
			MH_ERROR_NONE &&
		mh_net_drive_capture(&local, NULL, NULL, NULL) !=
			MH_ERROR_NONE);

	mh_collision_free(&col);

	return failures != 0;
}
