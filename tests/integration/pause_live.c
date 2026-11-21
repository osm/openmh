#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/media_audio.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/race/race.h"
#include "client/settings/render_choice.h"
#include "client/settings/store.h"
#include "engine/audio/race_playback.h"
#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/ui/pause.h"

static struct mh_race_context race_context;
static struct mh_field field;
static struct mh_particles restart_particles[MH_TRAFFIC_MAX_CARS];
static struct mh_particle_system restart_system;

struct fixture {
	unsigned int	frame, restarts, warm, horn_stage, horn_pulses, horn_quiet;
	int			continuous, repeat_probe, previous_menu;
	SDL_Keycode		held, pending;
	Uint64			probe_start;
	float			expected_distance;
	unsigned int		hud_stage, hud_frames, names_stage;
	int			hud_failed;
	struct mh_drive_frame	restart_sample;
	int			restart_clock_probe;
};

static void
hud_check(void *context, struct mh_hud_state *hud)
{
	struct fixture *fixture;
	unsigned long clock;

	fixture = (struct fixture *)context;
	fixture->hud_frames++;

	if (mh_clock_word(mh_drive_clock_ms(&field.cars[0]), &clock, NULL) !=
			MH_ERROR_NONE ||
		hud->clock_ms != clock)
		fixture->hud_failed = 1;

	if (race_context.settings == NULL)
		return;

	if (hud->info_detail != 1 || hud->map_mode != 1U ||
		hud->checkpoint_info != 0U ||
		field.cars[0].race.split_hold_ms != 1250)
		fixture->hud_failed = 1;
}

static enum mh_error_code
check_lighting(struct mh_error *error_out)
{
	struct mh_vertex *saved;
	size_t bytes;
	enum mh_error_code status;

	bytes = race_context.scene.assets.meshes[0].vertex_count *
		sizeof(*saved);
	saved = (struct mh_vertex *)malloc(bytes);

	if (saved == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot allocate lighting fixture");

	status = mh_race_scene_pause(
		&race_context.scene, 0, race_context.settings, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(saved, race_context.scene.assets.meshes[0].vertices,
			bytes);
		status = mh_race_scene_pause(&race_context.scene, 1,
			race_context.settings, error_out);

		if (status == MH_ERROR_NONE &&
			memcmp(saved,
				race_context.scene.assets.meshes[0].vertices,
				bytes) == 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"pausing did not dim world colors");

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_pause(&race_context.scene, 1,
				race_context.settings, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_pause(&race_context.scene, 0,
				race_context.settings, error_out);

		if (status == MH_ERROR_NONE &&
			memcmp(saved,
				race_context.scene.assets.meshes[0].vertices,
				bytes) != 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"resuming did not restore immutable world colors");
	}

	free(saved);

	return status;
}

static enum mh_error_code
key_event(SDL_Keycode keycode, int down, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	if (keycode == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"fixture driving window has no keyboard focus");

	memset(&event, 0, sizeof(event));
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID(window);
	event.key.key = keycode;
	event.key.scancode = SDL_GetScancodeFromKey(keycode, NULL);
	event.key.down = down != 0;

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot queue fixture key");
}

static enum mh_error_code
press(struct fixture *fixture, SDL_Keycode keycode, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = key_event(fixture->held, 0, error_out);

	/*
	 * Consecutive taps of the same key need a released poll between them.
	 * A release/re-press inside one poll is still held to the original.
	 */
	if (keycode != 0 && fixture->held == keycode) {
		fixture->pending = keycode;
		fixture->held = 0;
		return status;
	}

	fixture->held = keycode;

	if (status != MH_ERROR_NONE)
		return status;

	return key_event(keycode, 1, error_out);
}

static enum mh_error_code
restart(void *context, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_particle_input particles;
	unsigned int i;
	enum mh_error_code status;

	fixture = (struct fixture *)context;
	fixture->restarts++;

	if (fixture->continuous)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	fixture->restart_sample = field.cars[0].frame;

	if (!fixture->restart_sample.pending)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"restart did not stage the entry clock");

	/*
	 * Existing world particles belong to this race, including slots which
	 * no longer emit. Seed real emitters before entering the reset path.
	 */
	memset(&particles, 0, sizeof(particles));
	particles.rotation[0] = particles.rotation[4] = particles.rotation[8] =
		1;
	particles.tyre_id[0] = 1;
	particles.tyre_id[1] = 2;
	particles.spark_id[0] = 1;
	particles.spark_gain[0] = 1;
	race_context.scene.visuals.particle_system.random = 0x12345678UL;

	for (i = 0; i < MH_TRAFFIC_MAX_CARS; i++) {
		status = mh_particles_emit(&particles,
			&race_context.scene.visuals.particle_system,
			&race_context.scene.visuals.particles[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	race_context.scene.visuals.particle_mask = 255;
	race_context.scene.visuals.particle_timer = .043f;
	memcpy(restart_particles, race_context.scene.visuals.particles,
		sizeof(restart_particles));
	restart_system = race_context.scene.visuals.particle_system;
	status = mh_field_reset(&field, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (field.cars[0].frame.seconds != fixture->restart_sample.seconds ||
		field.cars[0].frame.time.now_ms !=
			fixture->restart_sample.time.now_ms ||
		field.cars[0].frame.time.elapsed !=
			fixture->restart_sample.time.elapsed)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"physical restart changed the sampled clock");

	fixture->restart_clock_probe = 1;
	SDL_Delay(25); /* Controlled reset work must reach the second sample. */

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	static const SDL_Keycode local_keys[] = {SDLK_ESCAPE, SDLK_DOWN,
		SDLK_RETURN, 0, SDLK_ESCAPE, SDLK_DOWN, SDLK_DOWN, SDLK_RETURN,
		SDLK_LEFT, SDLK_LEFT, SDLK_DOWN, SDLK_LEFT, SDLK_DOWN,
		SDLK_LEFT, SDLK_ESCAPE, SDLK_ESCAPE, SDLK_ESCAPE, SDLK_UP,
		SDLK_UP, SDLK_RETURN, SDLK_LEFT, SDLK_DOWN, SDLK_LEFT,
		SDLK_RIGHT, SDLK_ESCAPE, SDLK_ESCAPE, 0, SDLK_ESCAPE, SDLK_UP,
		SDLK_RETURN};

	static const SDL_Keycode network_keys[] = {SDLK_ESCAPE, SDLK_DOWN,
		SDLK_RETURN, SDLK_ESCAPE, SDLK_ESCAPE, 0, SDLK_RETURN,
		SDLK_ESCAPE, SDLK_UP, SDLK_RETURN};

	struct fixture *fixture;
	unsigned int sample;
	unsigned long ticks;
	SDL_Keycode pending;
	enum mh_error_code status;
	int paused;
	struct mh_drive_frame preview;
	struct mh_frame_schedule schedule;

	fixture = (struct fixture *)context;
	*steps = 0;
	*done = 0;

	if (fixture->restart_clock_probe != 0) {
		if (memcmp(restart_particles,
			race_context.scene.visuals.particles,
			sizeof(restart_particles)) ||
			memcmp(&restart_system,
				&race_context.scene.visuals.particle_system,
				sizeof(restart_system)) ||
			race_context.scene.visuals.particle_mask != 255 ||
			race_context.scene.visuals.particle_timer !=
				(fixture->restart_clock_probe == 1
						? .043f
						: (float)((long double).043f +
							fixture->restart_sample
								.time
								.elapsed)))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"restart lost particle pools, animation cursors, RNG or cadence");
	}

	if (fixture->restart_clock_probe == 1) {
		if (!field.cars[0].frame.pending || elapsed != 0 ||
			field.cars[0].frame.seconds <
				fixture->restart_sample.seconds + .020 ||
			field.cars[0].frame.time.now_ms !=
				fixture->restart_sample.time.now_ms ||
			field.cars[0].frame.time.elapsed !=
				fixture->restart_sample.time.elapsed ||
			field.cars[0].frame.time.go_ms !=
				(field.cars[0].countdown
						? 0
						: field.cars[0]
							.frame.intro_ms)) {
			fprintf(stderr,
				"restart clock: pending %d elapsed %.12Lg seconds %.17g -> %.17g now %.9g -> %.9g dt %.9g -> %.9g GO %lu\n",
				field.cars[0].frame.pending, elapsed,
				fixture->restart_sample.seconds,
				field.cars[0].frame.seconds,
				(double)fixture->restart_sample.time.now_ms,
				(double)field.cars[0].frame.time.now_ms,
				(double)fixture->restart_sample.time.elapsed,
				(double)field.cars[0].frame.time.elapsed,
				field.cars[0].frame.time.go_ms);
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"restart lost its second counter sample or retained interval");
		}

		fixture->restart_sample = field.cars[0].frame;
	} else if (fixture->restart_clock_probe == 2) {
		status = mh_drive_frame_sample(
			&field.cars[0], elapsed, &preview, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (field.cars[0].frame.pending || preview.time.elapsed < .020f)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"following frame omitted the reset duration");

		fixture->restart_clock_probe = 0;
	}

	if (!fixture->continuous && action != MH_FRAME_SUSPEND) {
		if ((elapsed == 0) != fixture->previous_menu)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"menu transition did not retain the entry-frame clock gate");

		fixture->previous_menu = action == MH_FRAME_MENU;
	}

	/* Keep scripted presses off the same-millisecond boundary. */
	SDL_Delay(2);

	if (fixture->warm != 0) {
		fixture->warm--;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (fixture->pending != 0) {
		pending = fixture->pending;
		fixture->pending = 0;
		return press(fixture, pending, error_out);
	}

	if (race_context.settings != NULL && fixture->hud_stage < 3) {
		fixture->hud_stage++;
		return press(fixture,
			fixture->hud_stage == 1		  ? SDLK_M
				: fixture->hud_stage == 2 ? SDLK_G
							: 0,
			error_out);
	}

	if (race_context.settings != NULL && fixture->names_stage < 4) {
		fixture->names_stage++;
		return press(fixture, fixture->names_stage % 2 ? SDLK_N : 0,
			error_out);
	}

	if (!fixture->continuous && fixture->frame == 0 &&
		fixture->horn_stage < 3) {
		if (fixture->horn_stage == 0) {
			fixture->horn_stage = 1;
			fixture->probe_start = SDL_GetTicks();
			return press(fixture, SDLK_SPACE, error_out);
		}

		if (fixture->horn_stage == 1) {
			if (field.cars[0].horn)
				fixture->horn_pulses++;
			else
				fixture->horn_quiet++;

			if (SDL_GetTicks() - fixture->probe_start < 700) {
				SDL_Delay(10);
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
			}

			fixture->horn_stage = 2;

			return press(fixture, 0, error_out);
		}

		if (field.cars[0].horn || fixture->horn_pulses < 2 ||
			fixture->horn_quiet == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held horn did not use repeated input events");

		fixture->horn_stage = 3;
	}

	if (fixture->repeat_probe) {
		if (elapsed != 0 || !race_context.scene.visuals.pause_dim)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held menu arrow advanced local time");

		if (field.cars[0].view_percent == fixture->expected_distance) {
			if (SDL_GetTicks() - fixture->probe_start > 3000)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"held menu arrow failed to repeat");

			SDL_Delay(10);

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (field.cars[0].view_percent !=
			fixture->expected_distance - 5)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held menu arrow skipped a repeat step");

		fixture->repeat_probe = 0;
		fixture->expected_distance -= 5;

		return press(fixture, SDLK_ESCAPE, error_out);
	}

	sample = fixture->frame++;

	if (fixture->continuous) {
		if (sample == 10) {
			if (fixture->restarts != 1)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"network Quit did not return to lobby");

			*done = 1;

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (sample >= sizeof(network_keys) / sizeof(network_keys[0]) ||
			elapsed <= 0 || action != MH_FRAME_RUN)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network pause stopped session time");

		if (fixture->restarts != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network Restart was not disabled");

		if (race_context.scene.visuals.pause_dim !=
			(sample == 1 || sample == 2 || sample == 3 ||
				sample == 5 || sample == 8 || sample == 9))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network pause/results brightness differs");

		if (sample == 5) {
			field.cars[0].race.finished = 1;
			field.standings.cars[0].finished = 1;
		}

		return press(fixture, network_keys[sample], error_out);
	}

	if (sample >= sizeof(local_keys) / sizeof(local_keys[0]))
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "local Quit failed");

	paused = !(sample == 0 || sample == 3 || sample == 4 || sample == 16 ||
		sample == 26 || sample == 27);

	if ((action == MH_FRAME_MENU) != paused)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"local menu did not freeze/resume time");

	if (race_context.scene.visuals.pause_dim != (paused || sample == 3))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"menu brightness timing differs");

	if (fixture->restarts != (sample >= 3 ? 1U : 0U))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"local Restart callback differs");

	if (sample >= 14 &&
		(field.cars[0].view_percent != fixture->expected_distance ||
			fabs(mh_drive_camera_far(&field.cars[0]) -
				(float)(race_context.scene.assets.track
						.view_distance *
					fixture->expected_distance * .01)) >
				.0001))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"live view distance was not applied");

	if (controls->throttle != 0 || controls->brake != 0 ||
		controls->steering != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"menu navigation leaked into driving controls");

	ticks = field.cars[0].ticks;
	status = mh_field_advance(
		&field, controls, elapsed, action, steps, error_out);

	if (status == MH_ERROR_NONE && fixture->restart_clock_probe == 1) {
		status = mh_frame_schedule(fixture->restart_sample.time.elapsed,
			&schedule, error_out);

		if (status == MH_ERROR_NONE &&
			(field.cars[0].frame.pending ||
				*steps != schedule.count ||
				field.cars[0].frame.seconds !=
					fixture->restart_sample.seconds))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"restart frame did not consume its retained interval exactly once");

		fixture->restart_clock_probe = 2;
	}

	if (status == MH_ERROR_NONE && paused &&
		(field.cars[0].ticks != ticks || *steps != 0))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"paused simulation advanced");

	if (sample == 3)
		fixture->warm = 4; /* Restart calls the original input reset. */

	if (sample == 14) {
		fixture->repeat_probe = 1;
		fixture->probe_start = SDL_GetTicks();
	} else if (status == MH_ERROR_NONE)
		status = press(fixture, local_keys[sample], error_out);

	return status;
}

static enum mh_error_code
check_audio(const char *root, const struct mh_audio_options *options,
	struct mh_error *error_out)
{
	struct mh_audio *audio;
	struct mh_pause_state state;
	struct mh_pause_settings settings;
	struct mh_pause_input input;
	struct mh_pause_output output;
	enum mh_error_code status;
	unsigned int i;
	int success;

	audio = mh_audio_open(
		root, &race_context.scene.assets.track, options, 0);

	if (audio == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"dummy audio device or original audio assets unavailable");

	success = !mh_audio_music_playing(audio);
	mh_pause_init(&state, NULL);
	memset(&settings, 0, sizeof(settings));
	memset(&input, 0, sizeof(input));
	settings.distance = 100;
	settings.perspective = 16;
	input.restart_ok = 1;
	input.keys = MH_PAUSE_TOGGLE;
	status = mh_pause_update(&state, &settings, &input, &output, error_out);
	success = success && status == MH_ERROR_NONE &&
		mh_audio_menu(audio, &output, 1);

	for (i = 0; i < 6 && success; i++) {
		success = mh_audio_update(audio, &field, 0, 0, 0, 0);
		SDL_Delay(12);
	}

	state.page = 2;
	state.selected[2] = 1;
	input.keys = MH_PAUSE_RIGHT;
	status = mh_pause_update(&state, &settings, &input, &output, error_out);
	success = success && status == MH_ERROR_NONE &&
		mh_audio_menu(audio, &output, 1) &&
		mh_audio_music_playing(audio);
	state.page = 0;
	input.keys = MH_PAUSE_TOGGLE;
	status = mh_pause_update(&state, &settings, &input, &output, error_out);
	success = success && status == MH_ERROR_NONE &&
		mh_audio_menu(audio, &output, 0) &&
		mh_audio_update(audio, &field, 0, 0, 0, 0);
	mh_audio_reset(audio);
	success = success && mh_audio_music_playing(audio);
	mh_audio_close(audio);

	return success ? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
				"menu audio could not continue or unmute CD");
}

int
main(int argc, char *argv[])
{
	struct mh_drive drive;
	struct mh_audio_options options;
	struct mh_race_session_hooks session;
	struct fixture fixture;
	struct mh_settings defaults, expected, *preferences;
	struct mh_settings_store *store, *reopened;
	const struct mh_settings_catalog *catalog;
	struct stat before, after;
	char *directory, path[128];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int mode;
	unsigned long random;
	int success;

	if (argc != 2) {
		fprintf(stderr,
			"Usage: mh-pause-live GAME_ROOT (requires a video device)\n");
		return 2;
	}

	if ((directory = mh_test_directory("mh-pause-settings-")) == NULL)
		return 2;

	strcpy(path, directory);
	strcat(path, "/MotorHead.CFG");
	store = reopened = NULL;
	preferences = NULL;
	SDL_SetHintWithPriority(
		SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	memset(&options, 0, sizeof(options));
	options.effects_volume = 1;
	status = mh_race_context_load(argv[1], 1, 1, 1, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 0, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, field.count,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&race_context.scene, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_load(argv[1], &race_context.hud, &error);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load(argv[1], field.count,
			"Pause fixture", &race_context.hud.atlas,
			&race_context.results, &error);

	if (status == MH_ERROR_NONE)
		status = check_lighting(&error);

	if (status == MH_ERROR_NONE)
		status = check_audio(argv[1], &options, &error);

	success = status == MH_ERROR_NONE;

	for (mode = 0; mode < 3 && success; mode++) {
		SDL_SetHintWithPriority(
			SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
		memset(&fixture, 0, sizeof(fixture));
		fixture.continuous = mode == 1;
		fixture.warm = 4;
		fixture.expected_distance = 95;

		if (mode == 2) {
			status = mh_settings_init(&defaults, "Stored driver",
				255, 87, MH_RENDER_CHOICE_GLIDE, &error);
			random = 17;

			if (status == MH_ERROR_NONE)
				status = mh_settings_store_open(argv[1], path,
					&defaults, &random, &store, &error);

			if (status != MH_ERROR_NONE) {
				success = 0;
				break;
			}

			catalog = mh_settings_store_catalog(store);

			if (random != 17 || catalog->cars.count != 10 ||
				catalog->tracks.count != 8 ||
				catalog->horns.count == 0) {
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"installed settings catalog did not bind");
				success = 0;
				break;
			}

			preferences = mh_settings_store_values(store);
			preferences->car = 4;
			preferences->track = 2;
			preferences->automatic = 0;
			strcpy(preferences->session_password, "Keep me");
			strcpy(preferences->short_keys[9],
				"Keep this chat text");
			preferences->av.detail_mode = MH_DETAIL_CUSTOM;
			preferences->av.view_distance = 110;
			preferences->av.car_shading = 1;
			preferences->av.perspective = 16;
			preferences->av.info_detail = 1;
			preferences->av.checkpoint_info = 0;
			preferences->av.road_map = 1;
			preferences->av.checkpoint_delay = 1250;
			preferences->av.name_plates =
				1; /* Unbound N must preserve Flat. */
			race_context.settings = &preferences->av;
			expected = *preferences;
			fixture.expected_distance = 105;
		}

		memset(&session, 0, sizeof(session));
		session.context = &fixture;
		session.pause_with_ui = mode != 1;
		session.advance = advance;
		session.restart = restart;
		session.hud = hud_check;
		status = mh_field_reset(&field, 1, &error);

		if (status == MH_ERROR_NONE)
			status = mh_drive_view_distance(
				&field.cars[0], 100, &error);

		success = status == MH_ERROR_NONE &&
			mh_race_run_session(&race_context, &field, argv[1],
				&options, NULL, &session, NULL);

		if (success &&
			(fixture.restarts != 1 ||
				fixture.frame != (mode == 1 ? 11U : 30U))) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"interactive fixture exited prematurely");
			success = 0;
		}

		if (success &&
			(fixture.hud_frames == 0 || fixture.hud_failed)) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"game clock or saved HUD options did not reach live presentation");
			success = 0;
		}

		if (success && mode == 2) {
			expected.av.car_shading = 0;
			expected.av.perspective = 32;
			expected.av.view_distance = 100;
			expected.av.effects_volume = 253;
			expected.av.road_map = 1;
			expected.av.checkpoint_info = 0;

			if (memcmp(preferences, &expected, sizeof(expected)) !=
				0) {
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"live option changes altered unrelated configuration fields");
				success = 0;
			}

			if (success)
				status = mh_settings_store_save(store, &error);

			if (success && status == MH_ERROR_NONE)
				status = mh_settings_store_open(argv[1], path,
					&defaults, &random, &reopened, &error);

			if (success && status == MH_ERROR_NONE &&
				memcmp(mh_settings_store_values(reopened),
					&expected, sizeof(expected)) != 0)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"saved live options did not reload");

			if (success && status == MH_ERROR_NONE &&
				stat(path, &before) != 0)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"cannot inspect settings fixture");

			if (success && status == MH_ERROR_NONE)
				status = mh_settings_store_save(
					reopened, &error);

			if (success && status == MH_ERROR_NONE &&
				(stat(path, &after) != 0 ||
					before.st_ino != after.st_ino))
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"unchanged settings rewrote the file");

			if (status != MH_ERROR_NONE)
				success = 0;
		}
	}

	if (!success)
		fprintf(stderr, "Pause integration: %s\n",
			status != MH_ERROR_NONE ? error.message
						: "live race failed");

	mh_race_context_free(&race_context);
	mh_settings_store_close(store);
	mh_settings_store_close(reopened);

	if (access(path, F_OK) == 0 && unlink(path) != 0)
		success = 0;

	if (rmdir(directory) != 0)
		success = 0;

	if (success)
		puts("Live menu timing, held-arrow/horn events, restart/particle retention, persisted options, results, lobby return and dummy audio passed");

	return !success;
}
