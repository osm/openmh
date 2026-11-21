/*
 * Optional real-asset check; generated takes live only in a private /tmp
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
#include "backends/storage/memory_user_data.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_demo_setup.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/diagnostics/storage_demo_recording.h"
#include "client/session/demo_recording.h"
#include "client/session/demo_setup.h"
#include "engine/core/error.h"
#include "engine/input/binding.h"
#include "engine/input/control_type.h"
#include "engine/storage/demo_file.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "recording assets line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "recording assets line %d: %s\n",      \
				__LINE__, failure.message);                    \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context context, playback_assets;
static struct mh_field field, initial, held;
static struct mh_demo_race race;
static struct mh_demo_recording *recording;
static struct mh_demo_file *pending;

static int
prepare_recording_race(const char *root, int catchup)
{
	struct mh_error failure;
	enum mh_error_code status;
	const struct mh_car *roster[7];
	struct mh_drive drive;
	unsigned int i;

	TRY(mh_race_context_load(root, 1, 1, 1, &context, &failure));
	context.scene.assets.track.laps = catchup ? 2 : 1;
	TRY(mh_race_scene_load_ai(root, &context.scene, &failure));
	TRY(mh_race_scene_add_opponents(
		root, 1, 1, 7, &context.scene, &failure));
	TRY(mh_drive_start(&context.scene.assets.car,
		&context.scene.assets.world_collision,
		&context.scene.assets.materials, &context.scene.assets.route,
		&context.scene.assets.track, 0, 0, 1, &drive, &failure));
	TRY(mh_drive_enable_ai(
		&drive, &context.scene.assets.ai_path, 2, &failure));

	for (i = 0; i < 7; i++)
		roster[i] = &context.scene.assets.opponents[i];

	TRY(mh_field_start(&drive, roster, 7, &context.scene.assets.ai_path, 2,
		&field, &failure));

	if (catchup)
		TRY(mh_field_enable_catchup(&field, 2, &failure));

	TRY(mh_field_set_hulls(
		&field, context.scene.assets.car_hulls, field.count, &failure));
	mh_race_scene_bind_cameras(&context.scene, &field);
	TRY(mh_race_scene_bind_objects(&context.scene, &field, &failure));
	TRY(mh_hud_assets_load(root, &context.hud, &failure));
	TRY(mh_results_assets_load(root, 8, "Driver", &context.hud.atlas,
		&context.results, &failure));
	strcpy(context.results.profiles[0].team, "TEST");

	for (i = 0; i < field.count; i++)
		strcpy(context.results.profiles[i].horn,
			i % 2 ? "Honker1" : "Honker2");

	initial = field;

	return 1;
}

static int
advance_recording(unsigned int frames)
{
	struct mh_error failure;
	enum mh_error_code status;
	struct mh_engine_input controls;
	unsigned int frame, steps;

	memset(&controls, 0, sizeof(controls));

	for (frame = 0; frame < frames; frame++) {
		TRY(mh_demo_recording_advance(recording, &field, &controls,
			2 * MH_DRIVE_DT, 0, &steps,
			&context.scene.visuals.particle_system.random,
			&failure));
		CHECK(steps == 1);
		TRY(mh_race_scene_skids(&context.scene, &field, &failure));
		TRY(mh_race_scene_update_particles(
			&context.scene, &field, &failure));
	}

	return 1;
}

static int
check_recording(const char *root, const char *path, int catchup)
{
	struct mh_error failure;
	struct mh_demo demo;
	struct mh_file file;
	struct mh_engine_input controls;
	struct mh_demo_frame frame;
	struct mh_net_vehicle vehicle;
	struct mh_demo_record_sample sample;
	struct mh_control_binding_set bindings;
	unsigned long finish[8], best[8], held_random;
	unsigned int i, steps, frames, frame_count;
	int saved;
	FILE *conflict;
	enum mh_error_code status;

	CHECK(prepare_recording_race(root, catchup));
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));
	memset(&controls, 0, sizeof(controls));
	field.cars[0].wheels.angle[0] = 1e7;
	held = field;
	held_random = context.scene.visuals.particle_system.random;
	steps = 999;
	CHECK(mh_demo_recording_advance(recording, &field, &controls, 6.0005, 0,
		&steps, &context.scene.visuals.particle_system.random,
		NULL) != MH_ERROR_NONE);
	CHECK(steps == 999 &&
		context.scene.visuals.particle_system.random == held_random &&
		memcmp(&field, &held, sizeof(field)) == 0);
	CHECK(mh_demo_recording_active(recording) &&
		mh_demo_recording_start_ticks(recording) == 0 &&
		mh_demo_recording_write_error(recording, NULL) ==
			MH_ERROR_NONE);
	field = initial;

	/* Exactly six seconds: the gate is still closed. */
	CHECK(advance_recording(360));
	TRY(mh_demo_recording_prepare(recording, &field, &pending, &failure));
	recording = NULL;
	CHECK(pending == NULL && access(path, F_OK) != 0);
	field = initial;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));

	/* Enough frames for an old take; Home must discard it. */
	CHECK(advance_recording(600));
	held = field;
	conflict = fopen(path, "wb");
	CHECK(conflict != NULL && fclose(conflict) == 0);
	CHECK(mh_demo_recording_restart(recording, &field, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&field, &held, sizeof(field)) == 0 && unlink(path) == 0);
	TRY(mh_demo_recording_restart(recording, &field, &failure));
	CHECK(field.cars[0].ticks == 600 && field.cars[0].start_ticks == 0);
	mh_race_scene_skids_reset(&context.scene);

	/* New six-second gate, despite ten lifetime seconds. */
	CHECK(advance_recording(360));
	TRY(mh_demo_recording_finish(recording, &field, &saved, &failure));
	recording = NULL;
	CHECK(!saved && access(path, F_OK) != 0);
	field = initial;
	memset(&bindings, 0, sizeof(bindings));
	bindings.codes[MH_CONTROL_LEFT] = 0x1100;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles,
		&bindings, &recording, &failure));
	CHECK(advance_recording(600));
	TRY(mh_demo_recording_restart(recording, &field, &failure));
	mh_race_scene_skids_reset(&context.scene);
	memset(&controls, 0, sizeof(controls));
	frame_count = catchup ? 16000 : 8000;

	for (frames = 0; frames < frame_count; frames++) {
		CHECK(advance_recording(1));

		if (frames == 500) {
			held = field;
			TRY(mh_demo_recording_advance(recording, &field,
				&controls, 30, 1, &steps,
				&context.scene.visuals.particle_system.random,
				&failure));
			CHECK(steps == 0);
			CHECK(memcmp(&field, &held, sizeof(field)) == 0);

			for (i = 0; i < 100; i++)
				TRY(mh_demo_recording_frame(
					recording, &field, &failure));
		}
	}

	CHECK(field.cars[0].ticks == 600 + frame_count);
	CHECK(field.standings.complete);

	for (i = 0; i < 8; i++) {
		CHECK(field.cars[i].race.finished);
		finish[i] = field.cars[i].race.now_ms;
		best[i] = field.cars[i].race.best_ms;
	}

	TRY(mh_demo_recording_finish(recording, &field, &saved, &failure));
	recording = NULL;
	CHECK(saved);
	TRY(mh_file_read(path, &file, &failure));
	TRY(mh_demo_read(file.data, file.size, &demo, &failure));
	CHECK(demo.version == MH_DEMO_VERSION_30);
	CHECK(demo.player_count == 8 &&
		demo.frame_count == (catchup ? 3936U : 1920U));
	CHECK(demo.laps == (catchup ? 2U : 1U));
	CHECK(demo.flags == (unsigned long)catchup);
	CHECK(demo.players[0].control_type ==
		2); /* Applied mouse identity survives Home. */
	CHECK(strcmp(demo.players[0].name, "Driver") == 0 &&
		strcmp(demo.players[0].team, "TEST") == 0);

	for (i = 0; i < 8; i++) {
		if (i != 0)
			CHECK(demo.players[i].control_type == 0);

		CHECK(strcmp(demo.players[i].horn,
			context.results.profiles[i].horn) == 0);
		CHECK(strcmp(demo.players[i].car, field.cars[i].car->name) ==
			0);
		CHECK(memcmp(demo.players[i].crossings,
			field.cars[i].race.splits,
			sizeof(demo.players[i].crossings)) == 0);
	}

	/*
	 * The unrounded six-second gate after Home opens one frame earlier
	 * here; the first >62.5ms writer sample is 66 integer milliseconds.
	 */
	TRY(mh_demo_frame_read(&demo, 0, &frame, &failure));
	CHECK(frame.milliseconds == 66);
	TRY(mh_demo_capture_sample(&field, &demo, &sample, &failure));
	TRY(mh_demo_frame_read(&demo, demo.frame_count - 1, &frame, &failure));

	for (i = 0; i < 8; i++) {
		const unsigned char *body;

		body = frame.vehicles[i];
		CHECK(body != NULL);
		TRY(mh_net_vehicle_decode(body, 30, &vehicle, &failure));
		CHECK(memcmp(body, sample.bodies[i], 30) ==
			0); /* final padding uses exit pose */
	}

	TRY(mh_demo_setup_load(
		root, &demo, 0, 1, 0, 1, &playback_assets, &race, &failure));

	for (i = 0; i < frame_count + 300; i++)
		TRY(mh_demo_race_frame(&race, &controls, 2 * MH_DRIVE_DT, 0,
			&steps, &failure));

	CHECK(!race.playback.active &&
		race.playback.next_frame == demo.frame_count &&
		race.field.standings.complete);

	for (i = 0; i < 8; i++) {
		CHECK(race.field.cars[i].race.finished &&
			race.field.cars[i].race.now_ms == finish[i]);
		CHECK(race.field.cars[i].race.best_ms == best[i]);
		CHECK(race.field.standings.cars[i].position ==
			field.standings.cars[i].position);
		CHECK(memcmp(race.field.cars[i].race.splits,
			field.cars[i].race.splits,
			sizeof(demo.players[i].crossings)) == 0);
	}

	free(file.data);
	CHECK(unlink(path) == 0);
	field = initial;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));
	field.cars[0].ticks = 800;
	TRY(mh_demo_recording_frame(recording, &field, &failure));
	field.cars[0].ticks = 799;
	CHECK(mh_demo_recording_frame(recording, &field, NULL) !=
		MH_ERROR_NONE);
	status = mh_demo_recording_finish(recording, &field, &saved, NULL);
	recording = NULL;
	CHECK(status != MH_ERROR_NONE && !saved && access(path, F_OK) != 0);
	field = initial;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));
	CHECK(mh_test_recording_failure(&context, &field, NULL, &recording, 0));
	CHECK(access(path, F_OK) != 0);
	field = initial;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));
	CHECK(mh_test_recording_failure(&context, &field, NULL, &recording, 1));
	CHECK(access(path, F_OK) != 0);
	field = initial;
	TRY(mh_demo_recording_open(path, &field, context.results.profiles, NULL,
		&recording, &failure));
	CHECK(advance_recording(600));
	TRY(mh_demo_recording_prepare(recording, &field, &pending, &failure));
	recording = NULL;
	CHECK(pending != NULL && access(path, F_OK) != 0);

	/*
	 * The frontend frees the race before the user names the private take.
	 */
	mh_race_context_free(&context);
	memset(&field, 0, sizeof(field));
	TRY(mh_demo_file_publish(pending, &failure));
	mh_demo_file_abort(pending);
	pending = NULL;
	TRY(mh_file_read(path, &file, &failure));
	TRY(mh_demo_read(file.data, file.size, &demo, &failure));
	CHECK(demo.version == MH_DEMO_VERSION_30);
	CHECK(demo.player_count == 8 && demo.frame_count >= 64);
	CHECK(demo.players[0].control_type ==
		1); /* No applied profile retains the diagnostic default. */
	free(file.data);
	CHECK(unlink(path) == 0);

	return 1;
}

static int
check_memory_recording(const char *root)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory, *editor;
	struct mh_data_file *conflict;
	struct mh_control_binding_set bindings;
	struct mh_file bytes;
	struct mh_demo demo;
	struct mh_error failure;
	enum mh_error_code status;
	unsigned long random;

	CHECK(prepare_recording_race(root, 0));
	TRY(mh_memory_user_data_open(&data, &failure));
	TRY(mh_user_data_create_directory(data, "takes", &failure));
	TRY(mh_user_data_open_directory(
		data, "takes", root, &directory, &failure));
	memset(&bindings, 0, sizeof(bindings));
	bindings.codes[MH_CONTROL_LEFT] = MH_BINDING_JOYSTICK;
	TRY(mh_demo_recording_open_in(directory, "Race.MDE", &field,
		context.results.profiles, &bindings, &recording, &failure));
	CHECK(advance_recording(600));
	CHECK(mh_user_data_read(data, "takes/Race.MDE", 65536, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);
	TRY(mh_user_data_open_directory(
		data, "takes", root, &editor, &failure));
	TRY(mh_data_directory_create(editor, "Race.MDE", "occupied", 8, NULL,
		NULL, &conflict, &failure));
	held = field;
	random = context.scene.visuals.particle_system.random;
	CHECK(mh_demo_recording_restart(recording, &field, NULL) ==
		MH_ERROR_EXISTS);
	CHECK(memcmp(&field, &held, sizeof(field)) == 0 &&
		context.scene.visuals.particle_system.random == random &&
		mh_demo_recording_active(recording));
	TRY(mh_data_file_remove(conflict, &failure));
	mh_data_file_close(conflict);
	mh_data_directory_close(editor);
	TRY(mh_demo_recording_restart(recording, &field, &failure));
	CHECK(!mh_demo_recording_start_notice(recording));
	CHECK(advance_recording(600));
	TRY(mh_demo_recording_prepare(recording, &field, &pending, &failure));
	recording = NULL;
	CHECK(pending != NULL);

	/*
	 * The take owns its directory after the race assets and recorder are
	 * gone.
	 */
	mh_race_context_free(&context);
	memset(&field, 0, sizeof(field));
	TRY(mh_demo_file_publish(pending, &failure));
	mh_demo_file_abort(pending);
	pending = NULL;
	TRY(mh_user_data_read(
		data, "takes/Race.MDE", 1024 * 1024, &bytes, &failure));
	TRY(mh_demo_read(bytes.data, bytes.size, &demo, &failure));
	CHECK(demo.frame_count >= 64 && demo.player_count == 8);
	CHECK(demo.players[0].control_type == MH_CONTROL_JOYSTICK);
	free(bytes.data);
	mh_memory_user_data_close(data);

	return 1;
}

int
main(int argc, char *argv[])
{
	char *directory, path[128];
	int success;

	if (argc < 2 || argc > 3 ||
		(argc == 3 && strcmp(argv[2], "--catch-up") != 0) ||
		(directory = mh_test_directory("mh-recording-assets-")) == NULL)
		return 1;

	strcpy(path, directory);
	strcat(path, "/take.MDE");
	success = check_recording(argv[1], path, argc == 3);

	if (success)
		success = check_memory_recording(argv[1]);

	mh_demo_recording_abort(recording);
	mh_demo_file_abort(pending);
	mh_race_context_free(&playback_assets);
	mh_race_context_free(&context);

	if (rmdir(directory) != 0) {
		fprintf(stderr, "leftover recording in %s\n", directory);
		success = 0;
	}

	if (success)
		puts("recording providers: memory restart/handoff, pause, short-take cleanup, Home restart, eight-car playback and private take handoff passed");

	return success ? 0 : 1;
}
