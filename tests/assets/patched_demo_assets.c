/* Synthetic large rosters borrowing real recording poses and game assets. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/font/provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "backends/storage/memory_user_data.h"
#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/runtime_demo_setup.h"
#include "client/diagnostics/runtime_race.h"
#include "client/race/race.h"
#include "client/session/demo_recording.h"
#include "client/session/demo_setup.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/vehicle_sound.h"
#include "engine/core/binary.h"
#include "engine/network/protocol.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #c,    \
				error.message == NULL ? "no diagnostic"        \
						      : error.message);        \
			return 1;                                              \
		}                                                              \
	} while (0)

#define FIXTURE_FRAMES 129
#define VEHICLE_BYTES 30

static struct mh_race_context context;
static struct mh_demo_race race, saved_race;
static struct mh_race_sound_assets sounds;
static struct mh_race_sound sound;
static struct mh_ui_list list;
static struct mh_race_info info;
static unsigned int resize_frames;

static void
show_info(void *unused, struct mh_hud_state *state)
{
	(void)unused;
	state->show_info = 1;
	state->info = info;
}

static enum mh_error_code
resize_frame(void *unused, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	SDL_Window **windows;
	int count, width, height, wanted_width, wanted_height, ok;

	(void)unused;
	(void)elapsed;
	resize_frames++;
	*done = resize_frames == 12;

	if (resize_frames == 2 || resize_frames == 7) {
		windows = SDL_GetWindows(&count);
		wanted_width = resize_frames == 2 ? 800 : 640;
		wanted_height = resize_frames == 2 ? 600 : 480;
		ok = windows != NULL && count == 1;

		if (ok)
			ok = SDL_SetWindowSize(
				windows[0], wanted_width, wanted_height) &&
				SDL_SyncWindow(windows[0]) &&
				SDL_GetWindowSize(
					windows[0], &width, &height) &&
				width == wanted_width &&
				height == wanted_height;

		SDL_free(windows);

		if (!ok)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"race window resize failed");
	}

	return mh_demo_race_frame(
		&race, controls, 1.0L / 60, action, steps, error_out);
}

static int
check_ghost_recording(void)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_demo_recording *recording;
	struct mh_profile human;
	struct mh_file bytes;
	struct mh_demo demo;
	struct mh_demo_frame frame;
	struct mh_engine_input controls;
	struct mh_error error;
	unsigned int i, steps, slot, ghost;
	struct mh_drive cars[MH_RACE_MAX_CARS];
	const struct mh_drive *previous;
	unsigned long random;
	int saved;

	memset(&error, 0, sizeof(error));
	memset(&controls, 0, sizeof(controls));
	human = context.results.profiles[0];
	strcpy(human.horn, "Honker2");
	random = race.field.ai_random;
	CHECK(mh_memory_user_data_open(&data, &error) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "takes", &error) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "takes", "assets", &directory,
		&error) == MH_ERROR_NONE);
	CHECK(mh_demo_recording_open_ghost_in(directory, "large.MDE", &race,
		&human, NULL, &recording, &error) == MH_ERROR_NONE);
	CHECK(mh_demo_recording_advance(recording, &race.field, &controls, 7,
		MH_FRAME_RUN, &steps, &random, &error) == MH_ERROR_NONE);
	CHECK(mh_demo_recording_restart(recording, &race.field, &error) ==
		MH_ERROR_NONE);

	for (i = 0; i < 200; i++)
		CHECK(mh_demo_recording_advance(recording, &race.field,
			&controls, .1, MH_FRAME_RUN, &steps, &random,
			&error) == MH_ERROR_NONE);

	CHECK(mh_demo_recording_finish(
		recording, &race.field, &saved, &error) == MH_ERROR_NONE);
	CHECK(saved);
	CHECK(mh_user_data_read(data, "takes/large.MDE", 1024 * 1024, &bytes,
		&error) == MH_ERROR_NONE);
	CHECK(mh_demo_read(bytes.data, bytes.size, &demo, &error) ==
		MH_ERROR_NONE);
	CHECK(demo.version == MH_DEMO_VERSION_30 && demo.player_count == 13);
	CHECK(strcmp(demo.players[0].horn, "Honker2") == 0);

	for (i = 1; i < 13; i++) {
		slot = race.layout.recording[i];
		CHECK(strcmp(demo.players[i].name,
			race.playback.demo->players[slot].name) == 0);
		CHECK(strcmp(demo.players[i].horn,
			race.playback.demo->players[slot].horn) == 0);
	}

	for (i = 0; i < 13; i++)
		CHECK(memcmp(demo.players[i].crossings,
			race.field.cars[i].race.splits,
			sizeof(demo.players[i].crossings)) == 0);

	for (i = 0; i < demo.frame_count; i++) {
		CHECK(mh_demo_frame_read(&demo, i, &frame, &error) ==
			MH_ERROR_NONE);
		CHECK(frame.active_mask == 8191 &&
			frame.unavailable_mask == 8190);
		CHECK(frame.vehicles[12] != NULL);
	}

	for (i = 0; i < 13; i++) {
		previous = &race.field.cars[i];
		CHECK(mh_drive_start(previous->car, previous->col,
			previous->materials, previous->route,
			previous->track, previous->slot,
			previous->config.options.manual, 1, &cars[i],
			&error) == MH_ERROR_NONE);
	}

	for (ghost = 0; ghost < 2; ghost++) {
		CHECK(mh_demo_race_begin(&demo, (int)ghost, cars, 13,
			context.scene.assets.car_hulls, &saved_race,
			&error) == MH_ERROR_NONE);
		CHECK(mh_demo_race_frame(&saved_race, &controls, 6.0005,
			MH_FRAME_RUN, &steps, &error) == MH_ERROR_NONE);

		for (i = 0; i < 120; i++)
			CHECK(mh_demo_race_frame(&saved_race, &controls, .25,
				MH_FRAME_RUN, &steps,
				&error) == MH_ERROR_NONE);

		CHECK(!saved_race.playback.active &&
			saved_race.playback.next_frame == demo.frame_count);
	}

	free(bytes.data);
	mh_memory_user_data_close(data);

	return 0;
}

static int
check_demo_profile(const char *root, const struct mh_demo *demo,
	unsigned int renderer, int ghost, const char *directory)
{
	struct mh_settings settings;
	struct mh_audio_options audio;
	struct mh_race_session_hooks hooks;
	struct mh_engine_input controls;
	struct mh_error error;
	unsigned int i, steps, voice, phase;
	char path[1024];
	const char *profile;

	memset(&error, 0, sizeof(error));
	memset(&controls, 0, sizeof(controls));
	memset(&audio, 0, sizeof(audio));
	memset(&hooks, 0, sizeof(hooks));
	CHECK(mh_settings_init(&settings, "Human", 0, 0, MH_RENDER_CHOICE_GLIDE,
		&error) == MH_ERROR_NONE);
	settings.renderer = renderer;
	settings.av.name_plates = MH_AV_NAMES_TRANSPARENT;
	settings.av.road_map = MH_AV_INFO_ALL;
	settings.av.width = 640;
	settings.av.height = 480;
	CHECK(mh_demo_setup_load_settings(root, demo, ghost, 1, 0, 1, &settings,
		&context, &race, &error) == MH_ERROR_NONE);
	CHECK(race.field.count == 13);
	CHECK(mh_demo_setup_ui(root, "Human", &race, &context, &error) ==
		MH_ERROR_NONE);
	CHECK(context.results.count == 13);
	CHECK(strcmp(context.results.profiles[12].nick,
		demo->players[race.layout.recording[12]].name) == 0);
	CHECK(mh_race_scene_names_prepare_from(mh_font_fallback_source(),
		context.resources, root, &context.scene, &race.field,
		context.results.profiles, 13, &error) == MH_ERROR_NONE);
	CHECK(mh_demo_race_frame(&race, &controls, 6.0005, MH_FRAME_RUN, &steps,
		&error) == MH_ERROR_NONE);
	CHECK(mh_demo_race_frame(&race, &controls, .25, MH_FRAME_RUN, &steps,
		&error) == MH_ERROR_NONE);
	CHECK(mh_field_select_view(&race.field, 12, &error) == MH_ERROR_NONE);
	CHECK(mh_field_camera_mode(&race.field, MH_CAMERA_CHASE, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_field_frame_update(&race.field, 0, &error) == MH_ERROR_NONE);

	/* All thirteen horns and engines have independent logical records. */
	audio.horns[0] = "Honker1";
	mh_demo_setup_audio(&race, &audio);
	CHECK(strcmp(audio.horns[12],
		demo->players[race.layout.recording[12]].horn) == 0);
	CHECK(mh_race_sound_assets_load(root, audio.horns, &sounds, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_race_sound_init(22050, 1, &sound, &error) == MH_ERROR_NONE);

	for (i = 0; i < 13; i++) {
		race.field.cars[i].horn = 1;
		race.field.cars[i].vehicle.engine.rpm = 1000;
	}

	CHECK(mh_race_sound_vehicles(&sound, &sounds.bank, &race.field, 0, 0,
		&error) == MH_ERROR_NONE);
	CHECK(mh_vehicle_sound_positions(&sound, NULL, &race.field, &error) ==
		MH_ERROR_NONE);
	voice = mh_race_horn_voice(12);
	CHECK(sound.mixer.voices[voice].wave == &sounds.bank.horns[12].wave);
	CHECK(!sound.mixer.voices[MH_COUNTDOWN_GO].active);
	CHECK(!sound.mixer.voices[MH_RACE_VOICE_FINISH_COPY].active);
	mh_race_sound_assets_free(&sounds);
	CHECK(mh_race_info_state(&race.field, context.results.profiles, NULL,
		&info, &error) == MH_ERROR_NONE);
	CHECK(info.count == 13);
	CHECK(mh_results_draw(&context.results, mh_race_context_atlas(&context),
		&race.field.standings, 640, 480, "Thirteen entrants", &list));

	profile = renderer == MH_RENDER_CHOICE_SOFTWARE ? "software" : "glide";

	if (directory != NULL) {
		CHECK(strlen(directory) < sizeof(path) - 80);

		for (phase = 0; phase < 3; phase++) {
			hooks.initial_results = phase == 2;
			hooks.hud = phase == 1 ? show_info : NULL;
			sprintf(path, "%s/%s-%s-%u.bmp", directory, profile,
				ghost ? "ghost" : "watch", phase);
			CHECK(mh_race_capture_session(&context, &race.field,
				path, 0, root, &audio, &hooks, NULL));
		}

		memset(&hooks, 0, sizeof(hooks));
		hooks.advance = resize_frame;
		resize_frames = 0;
		CHECK(mh_race_run_session(&context, &race.field, root, &audio,
			NULL, &hooks, NULL));
		CHECK(resize_frames == 12);
	}

	CHECK(mh_demo_race_restart(&race, &error) == MH_ERROR_NONE);
	CHECK(race.field.count == 13 && race.field.viewed_entry == 0);

	if (ghost)
		CHECK(check_ghost_recording() == 0);

	mh_race_context_free(&context);
	printf("%s %s: thirteen-car assets, names, HUD/Info/results, horns, camera, restart and recording pass\n",
		profile, ghost ? "Ghost" : "Watch");

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_demo original, header, demo;
	struct mh_demo_frame frame;
	struct mh_net_vehicle vehicle;
	struct mh_error error;
	unsigned char *recording_bytes, *frame_bytes;
	size_t stride, size;
	unsigned int i, car, slot, renderer, ghost;

	memset(&error, 0, sizeof(error));

	if (argc != 3 && argc != 4) {
		fprintf(stderr,
			"Usage: mh-patched-demo-assets GAME_ROOT RECORDING [CAPTURE_DIR]\n");
		return 1;
	}

	CHECK(mh_file_read(argv[2], &file, &error) == MH_ERROR_NONE);
	CHECK(mh_demo_read(file.data, file.size, &original, &error) ==
		MH_ERROR_NONE);
	header = original;
	header.version = MH_DEMO_VERSION_30;
	header.player_count = 13;
	header.local_player = 12;
	slot = original.local_player;

	for (i = 0; i < 13; i++) {
		header.players[i] = original.players[slot];
		header.players[i].active = 1;
		sprintf(header.players[i].name, "Driver%02u", i + 1);
		strcpy(header.players[i].horn, i % 2 ? "Honker2" : "Honker1");
		memset(header.players[i].crossings, 0,
			sizeof(header.players[i].crossings));
	}

	stride = 8 + 13 * VEHICLE_BYTES;
	size = MH_DEMO_30_HEADER_BYTES + FIXTURE_FRAMES * stride;
	recording_bytes = (unsigned char *)calloc(size, 1);
	CHECK(recording_bytes != NULL);
	CHECK(mh_demo_header_write(&header, recording_bytes,
		MH_DEMO_30_HEADER_BYTES, &error) == MH_ERROR_NONE);

	for (i = 0; i < FIXTURE_FRAMES; i++) {
		CHECK(mh_demo_frame_read(&original, i % original.frame_count,
			&frame, &error) == MH_ERROR_NONE);
		CHECK(mh_net_vehicle_decode(frame.vehicles[slot], VEHICLE_BYTES,
			&vehicle, &error) == MH_ERROR_NONE);
		frame_bytes =
			recording_bytes + MH_DEMO_30_HEADER_BYTES + i * stride;
		mh_write_u32le(frame_bytes, 8191);
		mh_write_u32le(frame_bytes + 4, 100 + 100 * i);

		for (car = 0; car < 13; car++) {
			vehicle.position[0] += 2;
			CHECK(mh_net_vehicle_encode(&vehicle,
				frame_bytes + 8 + car * VEHICLE_BYTES,
				VEHICLE_BYTES, &error) == MH_ERROR_NONE);
		}
	}

	CHECK(mh_demo_read(recording_bytes, size, &demo, &error) ==
		MH_ERROR_NONE);

	for (renderer = 0; renderer < MH_RENDER_CHOICE_COUNT; renderer++)
		for (ghost = 0; ghost < 2; ghost++)
			CHECK(check_demo_profile(argv[1], &demo, renderer,
				(int)ghost,
				argc == 4 ? argv[3] : NULL) == 0);

	free(recording_bytes);
	free(file.data);

	return 0;
}
