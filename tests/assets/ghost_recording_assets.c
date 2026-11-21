/*
 * Original resources are read-only; generated takes live in a private
 * directory.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/recording_failure.h"
#include "../support/temporary.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/runtime_demo_setup.h"
#include "client/diagnostics/storage_demo_recording.h"
#include "client/session/demo_recording.h"
#include "client/session/demo_setup.h"
#include "client/session/playback_hud.h"
#include "engine/core/error.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/demo_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Ghost recording line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "Ghost recording line %d: %s\n",       \
				__LINE__, failure.message);                    \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context context, replay_assets;
static struct mh_demo_race race, initial, held, replay;
static struct mh_demo_recording *recording;
static struct mh_demo_file *pending;
static struct mh_file source, take;
static struct mh_playback_hud hud;

static int
advance_recording(unsigned int count)
{
	struct mh_error failure;
	enum mh_error_code status;
	struct mh_engine_input controls;
	unsigned int frame, steps;

	memset(&controls, 0, sizeof(controls));

	for (frame = 0; frame < count; frame++) {
		TRY(mh_demo_recording_advance(recording, &race.field, &controls,
			2 * MH_DRIVE_DT, 0, &steps,
			&context.scene.visuals.particle_system.random,
			&failure));
		CHECK(steps == 1);
		TRY(mh_playback_hud_frame(&hud, &context.hud, MH_VIEW_WIDTH,
			&race.field, &race, recording, &failure));
	}

	return 1;
}

static int
check_recording(const char *root, const char *path)
{
	struct mh_error failure;
	enum mh_error_code status;
	struct mh_demo demo, header;
	struct mh_profile human;
	struct mh_demo_record_sample sample;
	struct mh_demo_frame frame;
	struct mh_engine_input controls;
	char original[1024];
	struct mh_control_binding_set bindings;
	FILE *conflict;
	unsigned int i, steps;
	unsigned long notice_origin, held_random, restart_ticks, start_ticks;
	int saved;

	TRY(mh_asset_path(root, "Demos/JeePen.MDE", original, sizeof(original),
		&failure));
	TRY(mh_file_read(original, &source, &failure));
	TRY(mh_demo_read(source.data, source.size, &demo, &failure));

	/*
	 * Alter only the borrowed header view, never the installed recording.
	 */
	strcpy(demo.players[0].name, "Bj\xf6rn \xc5kesson");
	strcpy(demo.players[0].team, "\xc5\xc4\xd6");
	TRY(mh_demo_setup_load(
		root, &demo, 1, 2, 1, 1, &context, &race, &failure));
	TRY(mh_demo_setup_ui(
		root, "\xc5ke \xe5\xe4\xf6", &race, &context, &failure));
	CHECK(strcmp(context.results.profiles[0].name, "\xc5ke \xe5\xe4\xf6") ==
			0 &&
		strcmp(context.results.profiles[1].name,
			demo.players[0].name) == 0 &&
		strcmp(context.results.profiles[1].team,
			demo.players[0].team) == 0);
	CHECK(context.hud.message_advance['M'] == 26 &&
		context.hud.hardware_height == 12);
	mh_playback_hud_init(&hud);
	memset(&human, 0, sizeof(human));
	strcpy(human.name, "\xc5ke \xe5\xe4\xf6");
	strcpy(human.nick, human.name);
	strcpy(human.team, "\xe5\xe4\xf6");

	for (i = 0; i < 4; i++)
		human.colors[i] = 0x123456UL + i;

	initial = race;
	TRY(mh_demo_recording_open_ghost(
		path, &race, &human, NULL, &recording, &failure));
	CHECK(mh_demo_recording_start_ticks(recording) == 0);

	/*
	 * One sampled stall opens BOTH media gates at relative 6000.5ms, with
	 * an absolute notification time of 6001. Physics remains capped.
	 */
	memset(&controls, 0, sizeof(controls));
	race.field.cars[0].wheels.angle[0] = 1e7;
	held = race;
	held_random = context.scene.visuals.particle_system.random;
	steps = 999;
	CHECK(mh_demo_recording_advance(recording, &race.field, &controls,
		6.0005, 0, &steps,
		&context.scene.visuals.particle_system.random,
		NULL) != MH_ERROR_NONE);
	CHECK(steps == 999 &&
		context.scene.visuals.particle_system.random == held_random &&
		memcmp(&race, &held, sizeof(race)) == 0);
	CHECK(mh_demo_recording_active(recording) &&
		mh_demo_recording_start_ticks(recording) == 0 &&
		mh_demo_recording_write_error(recording, NULL) ==
			MH_ERROR_NONE);
	race = initial;
	TRY(mh_demo_recording_advance(recording, &race.field, &controls, 6.0005,
		0, &steps, &context.scene.visuals.particle_system.random,
		&failure));
	CHECK(steps == 10);
	TRY(mh_playback_hud_frame(&hud, &context.hud, MH_VIEW_WIDTH,
		&race.field, &race, recording, &failure));
	CHECK(race.playback.anchor_ms == 6001 &&
		mh_demo_recording_start_ticks(recording) == 10 &&
		mh_demo_recording_start_ms(recording) == 6001 &&
		hud.messages.revision == 2 &&
		hud.messages.rows[0].milliseconds == 6001 &&
		hud.messages.rows[1].milliseconds == 6001);
	TRY(mh_demo_recording_restart(recording, &race.field, &failure));
	mh_playback_hud_init(&hud);
	restart_ticks = race.field.cars[0].ticks;

	/*
	 * The retained half millisecond opens the new gate on frame 360.
	 * Restart truncates the intro anchor, not the lifetime clock.
	 */
	CHECK(restart_ticks == 10 && advance_recording(359));
	CHECK(!mh_demo_recording_start_notice(recording) &&
		hud.messages.revision == 0);
	CHECK(advance_recording(1));
	start_ticks = restart_ticks + 360;
	CHECK(mh_demo_recording_start_ticks(recording) == start_ticks &&
		hud.messages.revision == 2 &&
		strcmp(hud.messages.rows[0].text, "Race recording started!") ==
			0 &&
		strcmp(hud.messages.rows[1].text, "Ghost play started!") == 0 &&
		hud.messages.rows[0].milliseconds == 12001 &&
		hud.messages.rows[1].milliseconds == 12001);
	CHECK(advance_recording(240));
	held = race;
	conflict = fopen(path, "wb");
	CHECK(conflict != NULL && fclose(conflict) == 0);
	CHECK(mh_demo_recording_restart(recording, &race.field, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&race, &held, sizeof(race)) == 0 && unlink(path) == 0);
	CHECK(mh_demo_recording_start_ticks(recording) == start_ticks);
	TRY(mh_demo_recording_restart(recording, &race.field, &failure));
	mh_playback_hud_restart(&hud, &race.field);
	CHECK(mh_demo_recording_start_ticks(recording) == 0 &&
		hud.messages.revision == 2);
	CHECK(race.ticks == held.ticks &&
		race.field.cars[0].ticks == held.field.cars[0].ticks &&
		race.field.cars[0].frame.seconds ==
			held.field.cars[0].frame.seconds &&
		race.playback.anchor_ms == 0);
	CHECK(memcmp(&race.field.cars[1].vehicle,
		&initial.field.cars[1].vehicle,
		sizeof(struct mh_vehicle_state)) == 0);
	CHECK(advance_recording(359));
	CHECK(mh_demo_recording_start_ticks(recording) == 0 &&
		hud.messages.revision == 2);
	TRY(mh_demo_recording_prepare(
		recording, &race.field, &pending, &failure));
	recording = NULL;
	CHECK(pending == NULL && access(path, F_OK) != 0);
	notice_origin = (unsigned long)hud.clock_ms;
	TRY(mh_demo_race_restart(&race, &failure));
	mh_playback_hud_restart(&hud, &race.field);
	memset(&bindings, 0, sizeof(bindings));
	bindings.codes[MH_CONTROL_LEFT] = 0x1400;
	TRY(mh_demo_recording_open_ghost(
		path, &race, &human, &bindings, &recording, &failure));
	CHECK(advance_recording(600));
	CHECK(hud.clock_offset_ms == 0 && hud.messages.revision == 4 &&
		hud.messages.rows[0].milliseconds == notice_origin + 6000 &&
		hud.messages.rows[1].milliseconds == notice_origin + 6000);
	memset(&controls, 0, sizeof(controls));
	held = race;
	TRY(mh_demo_recording_advance(recording, &race.field, &controls, 30, 1,
		&steps, &context.scene.visuals.particle_system.random,
		&failure));
	CHECK(steps == 0);
	CHECK(memcmp(&race, &held, sizeof(race)) == 0);

	for (i = 0; i < 100; i++)
		TRY(mh_demo_recording_frame(recording, &race.field, &failure));

	CHECK(advance_recording(4800));
	TRY(mh_ghost_capture_header(&initial, &human, &header, &failure));
	TRY(mh_ghost_capture_sample(&race, &header, &sample, &failure));
	CHECK(race.field.cars[1].race.splits[2][0] ==
		demo.players[0].crossings[2][0]);
	CHECK(race.field.cars[1].race.splits[10][0] ==
		0); /* no future crossings copied */
	TRY(mh_demo_recording_finish(recording, &race.field, &saved, &failure));
	recording = NULL;
	CHECK(saved);
	TRY(mh_file_read(path, &take, &failure));
	TRY(mh_demo_read(take.data, take.size, &header, &failure));
	CHECK(header.player_count == 2 && header.local_player == 0 &&
		header.frame_count >= 64);
	CHECK(header.players[0].control_type ==
		3); /* Local joystick; remote identity remains copied. */
	CHECK(header.frame_count % 32 == 0 && header.flags == demo.flags &&
		header.laps == demo.laps);
	CHECK(strcmp(header.players[0].name, human.name) == 0 &&
		header.players[0].gear_mode == 0);
	CHECK(strcmp(header.players[0].team, human.team) == 0 &&
		header.players[0].colors[0] == 0x123456UL);
	CHECK(memcmp(header.players[1].name, demo.players[0].name,
		sizeof(header.players[1].name)) == 0);
	CHECK(memcmp(header.players[1].team, demo.players[0].team,
		sizeof(header.players[1].team)) == 0);
	CHECK(memcmp(header.players[1].colors, demo.players[0].colors,
		sizeof(header.players[1].colors)) == 0);
	CHECK(header.players[1].gear_mode == demo.players[0].gear_mode &&
		header.players[1].control_type == demo.players[0].control_type);
	TRY(mh_demo_frame_read(
		&header, header.frame_count - 1, &frame, &failure));
	CHECK(frame.active_mask == 3);

	for (i = 0; i < 2; i++) {
		CHECK(memcmp(frame.vehicles[i], sample.bodies[i], 30) == 0);
		CHECK(memcmp(header.players[i].crossings,
			race.field.cars[i].race.splits,
			sizeof(header.players[i].crossings)) == 0);
	}

	TRY(mh_demo_setup_load(
		root, &header, 0, 1, 0, 1, &replay_assets, &replay, &failure));

	for (i = 0; i < 6000; i++)
		TRY(mh_demo_race_frame(&replay, &controls, 2 * MH_DRIVE_DT, 0,
			&steps, &failure));

	CHECK(!replay.playback.active &&
		replay.playback.next_frame == header.frame_count);
	CHECK(replay.field.cars[1].race.best_ms ==
		race.field.cars[1].race.best_ms);
	CHECK(memcmp(replay.field.cars[1].race.splits,
		race.field.cars[1].race.splits,
		sizeof(header.players[1].crossings)) == 0);
	CHECK(unlink(path) == 0);
	race = initial;
	TRY(mh_demo_recording_open_ghost(
		path, &race, &human, NULL, &recording, &failure));
	CHECK(mh_test_recording_failure(
		&context, &race.field, &race, &recording, 0));
	CHECK(access(path, F_OK) != 0);
	race = initial;
	TRY(mh_demo_recording_open_ghost(
		path, &race, &human, NULL, &recording, &failure));
	CHECK(mh_test_recording_failure(
		&context, &race.field, &race, &recording, 1));
	CHECK(access(path, F_OK) != 0);
	race = initial;
	mh_playback_hud_init(&hud);
	TRY(mh_demo_recording_open_ghost(
		path, &race, &human, NULL, &recording, &failure));
	CHECK(advance_recording(600));
	TRY(mh_demo_recording_prepare(
		recording, &race.field, &pending, &failure));
	recording = NULL;
	CHECK(pending != NULL && access(path, F_OK) != 0);
	mh_race_context_free(&context);
	memset(&race, 0, sizeof(race));
	free(source.data);
	memset(&source, 0, sizeof(source));
	TRY(mh_demo_file_publish(pending, &failure));
	mh_demo_file_abort(pending);
	pending = NULL;
	free(take.data);
	memset(&take, 0, sizeof(take));
	TRY(mh_file_read(path, &take, &failure));
	TRY(mh_demo_read(take.data, take.size, &header, &failure));
	CHECK(header.player_count == 2 && header.frame_count >= 64);
	CHECK(unlink(path) == 0);

	return 1;
}

int
main(int argc, char *argv[])
{
	char *directory, path[128];
	int success;

	if (argc != 2 ||
		(directory = mh_test_directory("mh-ghost-recording-")) == NULL)
		return 1;

	strcpy(path, directory);
	strcat(path, "/take.MDE");
	success = check_recording(argv[1], path);
	mh_demo_recording_abort(recording);
	mh_demo_file_abort(pending);
	mh_race_context_free(&replay_assets);
	mh_race_context_free(&context);
	free(source.data);
	free(take.data);

	if (rmdir(directory) != 0) {
		fprintf(stderr, "leftover recording in %s\n", directory);
		success = 0;
	}

	if (success)
		puts("Ghost recording: restart, pause/gate, metadata, poses, crossings, playback and private take handoff passed");

	return success ? 0 : 1;
}
