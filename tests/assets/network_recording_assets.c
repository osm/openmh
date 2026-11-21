/*
 * Installed assets stay read-only; all generated takes use a private directory.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/runtime_demo_setup.h"
#include "client/diagnostics/runtime_network_setup.h"
#include "client/diagnostics/storage_demo_recording.h"
#include "client/session/demo_recording.h"
#include "client/session/demo_setup.h"
#include "client/session/network_setup.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "network recording line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		if ((x) != MH_ERROR_NONE) {                                    \
			fprintf(stderr, "network recording line %d: %s\n",     \
				__LINE__, failure.message);                    \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_net_session session;
static struct mh_net_session saved_session;
static struct mh_net_race race, initial, saved_race;
static struct mh_race_context context, replay_assets;
static struct mh_demo_race replay;
static struct mh_demo_recording *recording;
static struct mh_file take;

static int
advance(unsigned int frames)
{
	struct mh_error failure;
	struct mh_engine_input controls;
	unsigned int i, steps;

	memset(&controls, 0, sizeof(controls));
	controls.throttle = .5;

	for (i = 0; i < frames; i++) {
		TRY(mh_demo_recording_network_advance(recording, &session,
			&controls, 2 * MH_DRIVE_DT, race.now_ms + 1000.0 / 60,
			0, &steps, &failure));
		CHECK(steps == 1);
	}

	return 1;
}

static int
run(const char *root, const char *path, unsigned int local)
{
	struct mh_error failure;
	struct mh_net_session_config config;
	struct mh_track track;
	struct mh_car car;
	struct mh_demo header;
	struct mh_demo_frame frame;
	struct mh_net_vehicle vehicle;
	struct mh_engine_input controls;
	double positions[8][3], velocity[3], angle;
	unsigned int i, slot, steps;
	int saved;

	TRY(mh_track_config_load(root, 1, &track, &failure));
	TRY(mh_car_config_load(root, 1, &car, &failure));
	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, track.name);
	strcpy(config.profile.car, car.name);
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	TRY(mh_net_session_init(local == 0, &config, &session, &failure));
	session.phase = MH_NET_LOADING;
	session.player = local;
	session.roster.player_count = 8;
	session.roster.laps = 3;
	strcpy(session.roster.track, track.name);

	for (i = 0; i < 3; i++) {
		slot = i == 0 ? 0 : (i == 1 ? 3 : 7);
		session.roster.players[slot] = config.profile;
		session.roster.players[slot].flags = 1;
		session.roster.players[slot].value_a = i + 1;
		session.lobby.slots[slot].phase = MH_NET_IN_RACE;
		session.lobby.slots[slot].peer = slot + 1;
		session.roster.players[slot].gear_mode = i % 2;
		session.roster.players[slot].control_type = i + 1;
		session.roster.players[slot].values[0] = 0x123456UL + slot;
		sprintf(session.roster.players[slot].name, "Driver%u", slot);
	}

	TRY(mh_network_setup_load(
		root, &session, 0, &context, &race, &failure));
	initial = race;

	for (i = 0; i < race.field.count; i++) {
		slot = race.layout.player[i];
		TRY(mh_motion_display(&race.field.cars[i].vehicle.motion,
			positions[slot], velocity, &failure));
	}

	TRY(mh_demo_recording_open_network(
		path, root, &race, &session, &recording, &failure));
	CHECK(mh_demo_recording_restart(recording, &race.field, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &initial, sizeof(race)) == 0);

	/*
	 * Waiting well beyond six seconds must not consume the recording gate.
	 */
	CHECK(advance(1200));
	CHECK(!race.started && race.field.cars[0].ticks == 1200);
	CHECK(mh_demo_recording_start_ticks(recording) == 0);

	if (session.host) {
		/*
		 * Seed the prior readiness observation; the next game frame
		 * releases.
		 */
		session.ready_mask = 137;
		session.start.anchor_ms =
			(unsigned long)mh_drive_clock_ms(&race.field.cars[0]) -
			2000;
	} else
		session.start_pending = 1;

	CHECK(advance(360));
	CHECK(race.start_tick == 1200);
	CHECK(mh_demo_recording_start_ticks(recording) == 0);

	/*
	 * The gate opens in this next frame, but late presentation failure must
	 * preserve the session and leave the owned file/recorder unstarted.
	 */
	angle = race.field.cars[2].wheels.angle[0];
	race.field.cars[2].wheels.angle[0] = 1e7;
	saved_race = race;
	saved_session = session;
	steps = 999;
	memset(&controls, 0, sizeof(controls));
	CHECK(mh_demo_recording_network_advance(recording, &session, &controls,
		1.0 / 60, race.now_ms + 1000.0 / 60, 0, &steps,
		NULL) != MH_ERROR_NONE);
	CHECK(steps == 999 && memcmp(&race, &saved_race, sizeof(race)) == 0);
	CHECK(memcmp(&session, &saved_session, sizeof(session)) == 0);
	CHECK(mh_demo_recording_start_ticks(recording) == 0 &&
		mh_demo_recording_active(recording));
	race.field.cars[2].wheels.angle[0] = angle;
	CHECK(advance(1));
	CHECK(mh_demo_recording_start_ticks(recording) == 1561);
	CHECK(advance(119));

	/* A peer leaves without ever delivering a ready history sample. */
	session.roster.players[7].flags = 0;
	CHECK(advance(720));
	CHECK(race.field.active_mask == 3);

	for (i = 0; i < race.field.count; i++)
		race.field.cars[i].race.splits[2][1] =
			17000 + race.layout.player[i];

	/*
	 * Return/reuse may change the live lobby. Saved metadata is already
	 * owned.
	 */
	memset(&session.roster, 0, sizeof(session.roster));
	session.phase = MH_NET_LOBBY;
	TRY(mh_demo_recording_finish(recording, &race.field, &saved, &failure));
	recording = NULL;
	CHECK(saved);
	TRY(mh_file_read(path, &take, &failure));
	TRY(mh_demo_read(take.data, take.size, &header, &failure));
	CHECK(header.version == MH_DEMO_VERSION_30);
	CHECK(header.local_player == local && header.player_count == 3 &&
		header.frame_count >= 64);
	CHECK(strcmp(header.players[7].horn, "Honker1") == 0);
	CHECK(header.players[7].active &&
		strcmp(header.players[7].name, "Driver7") == 0);
	CHECK(header.players[3].colors[0] == 0x123459UL &&
		header.players[3].gear_mode == 1);
	TRY(mh_demo_frame_read(&header, 0, &frame, &failure));
	CHECK(frame.active_mask == 0x89);
	TRY(mh_demo_frame_read(
		&header, header.frame_count - 1, &frame, &failure));
	CHECK(frame.active_mask == 9);

	for (i = 0; i < 8; i++) {
		CHECK((frame.vehicles[i] != NULL) ==
			(i == 0 || i == 3 || i == 7));

		if (frame.vehicles[i] == NULL)
			continue;

		CHECK(header.players[i].crossings[2][1] == 17000 + i);
		TRY(mh_net_vehicle_decode(
			frame.vehicles[i], 30, &vehicle, &failure));

		if (i != local)
			CHECK(vehicle.position[0] ==
					(double)(float)positions[i][0] &&
				vehicle.position[2] ==
					(double)(float)positions[i][2]);
	}

	TRY(mh_demo_setup_load(
		root, &header, 0, 1, 0, 1, &replay_assets, &replay, &failure));
	CHECK(replay.layout.recording[0] == local);
	memset(&controls, 0, sizeof(controls));

	for (i = 0; i < 2400; i++)
		TRY(mh_demo_race_frame(&replay, &controls, 2 * MH_DRIVE_DT, 0,
			&steps, &failure));

	CHECK(!replay.playback.active &&
		replay.playback.next_frame == header.frame_count);
	CHECK(replay.field.active_mask == 3);
	mh_race_context_free(&replay_assets);
	TRY(mh_demo_setup_load(
		root, &header, 1, 1, 0, 0, &replay_assets, &replay, &failure));
	CHECK(replay.layout.count == 4 &&
		replay.layout.recording[0] == MH_DEMO_HUMAN_SLOT &&
		replay.layout.recording[1] == 0 &&
		replay.layout.recording[2] == 3 &&
		replay.layout.recording[3] == 7);

	for (i = 0; i < 2400; i++)
		TRY(mh_demo_race_frame(&replay, &controls, 2 * MH_DRIVE_DT, 0,
			&steps, &failure));

	CHECK(!replay.playback.active &&
		replay.playback.next_frame == header.frame_count);
	CHECK(replay.field.active_mask == 7 &&
		replay.field.unavailable_mask == 6);
	mh_race_context_free(&replay_assets);
	free(take.data);
	memset(&take, 0, sizeof(take));
	CHECK(unlink(path) == 0);

	/* Early quit while still waiting removes the private take entirely. */
	race = initial;
	session.phase = MH_NET_WAITING;
	session.player = local;
	session.roster.player_count = 8;
	session.roster.laps = 3;
	strcpy(session.roster.track, track.name);

	for (i = 0; i < 3; i++) {
		slot = race.layout.player[i];
		session.roster.players[slot] = config.profile;
		session.roster.players[slot].flags = 1;
	}

	TRY(mh_demo_recording_open_network(
		path, root, &race, &session, &recording, &failure));
	TRY(mh_demo_recording_finish(recording, &race.field, &saved, &failure));
	recording = NULL;
	CHECK(!saved && access(path, F_OK) != 0);
	mh_race_context_free(&context);

	return 1;
}

int
main(int argc, char *argv[])
{
	char *directory, path[128];
	int success;

	if (argc != 2 ||
		(directory = mh_test_directory("mh-network-recording-")) ==
			NULL)
		return 1;

	strcpy(path, directory);
	strcat(path, "/take.MDE");
	success = run(argv[1], path, 0);

	if (success)
		success = run(argv[1], path, 3);

	mh_demo_recording_abort(recording);
	free(take.data);
	mh_race_context_free(&replay_assets);
	mh_race_context_free(&context);

	if (rmdir(directory) != 0) {
		fprintf(stderr, "leftover recording in %s\n", directory);
		success = 0;
	}

	if (success)
		puts("Network recording: sparse host/client slots, loading gate, initial remote poses, departures, checkpoints and playback to EOF pass");

	return success ? 0 : 1;
}
