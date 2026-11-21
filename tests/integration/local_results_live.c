/*
 * Real completed/aborted races through the interactive results-return path.
 * The X11 driver supplies input; no production simulation hooks are added.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_single_setup.h"
#include "client/race/race.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "interfaces/audio/horn_source.h"

static struct mh_profile_catalog catalog;
static struct mh_race_context race_context;
static struct mh_field field;
static unsigned int commits;
static unsigned int run_number, attempts;
static struct mh_standings retry_board;
static unsigned long retry_random;
static long double completed_clock;
static unsigned long completed_ticks;
static int finish_clock_committed;

static enum mh_error_code
accept(void *context, const struct mh_standings *standings,
	struct mh_error *error_out)
{
	unsigned int i, seen;

	(void)context;

	if (run_number == 0 && !finish_clock_committed)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"finish deadline discarded its sampled clock");

	if (standings->count != field.count)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "result roster changed");

	if (run_number == 3 || run_number == 6) {
		if (standings->complete ||
			standings->cars[0].position != field.count)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"abort did not place human last");

		seen = 0;

		for (i = 0; i < field.count; i++) {
			if (standings->cars[i].finished ||
				!standings->cars[i].estimated ||
				field.cars[i].race.finished ||
				standings->cars[i].position < 1 ||
				standings->cars[i].position > field.count)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"invalid unfinished result");

			seen |= 1U << standings->cars[i].position;
		}

		if (seen != (2U << field.count) - 2)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"duplicate abort position");
	} else {
		if (!standings->complete)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"incomplete results committed");

		for (i = 0; i < field.count; i++)
			if (!standings->cars[i].finished ||
				standings->cars[i].position !=
					field.standings.cars[i].position)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"committed result differs from completed race");
	}

	if (run_number == 3) {
		if (attempts++ == 0) {
			retry_board = *standings;
			retry_random = race_context.scene.visuals
					.particle_system.random;
			SDL_Delay(300); /* Music must keep filling during a
					stalled save. */
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"intentional fixture save failure");
		}

		if (memcmp(&retry_board, standings, sizeof(retry_board)) != 0 ||
			retry_random !=
				race_context.scene.visuals.particle_system
					.random)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"retry changed results or RNG");
	}

	commits++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance(void *context, const struct mh_engine_input *input, long double elapsed,
	enum mh_frame_action action, unsigned int *steps, int *done,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	long double now;

	(void)context;
	*done = 0;
	now = mh_drive_clock_ms(&field.cars[0]);

	if (run_number == 0 && action == MH_FRAME_SUSPEND &&
		now > completed_clock) {
		if (elapsed != 0 || field.cars[0].ticks != completed_ticks)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"finish exit ran physics or retained elapsed time");

		finish_clock_committed = 1;
	}

	status = mh_field_advance_random(&field, input, elapsed, action, steps,
		&race_context.scene.visuals.particle_system.random, error_out);

	if (status == MH_ERROR_NONE) {
		completed_clock = mh_drive_clock_ms(&field.cars[0]);
		completed_ticks = field.cars[0].ticks;
	}

	return status;
}

static enum mh_error_code
finish_race(unsigned long *random, struct mh_error *error_out)
{
	struct mh_engine_input input;
	unsigned long tick;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));

	for (tick = 0; tick < 60000 && !field.standings.complete; tick++) {
		field.ai_random = *random;
		status = mh_field_frame_tick_duration(&field, &input,
			tick % 2 == 0, tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0,
			error_out);

		if (status == MH_ERROR_NONE && tick % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*random = field.ai_random;
	}

	if (!field.standings.complete)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"results fixture did not finish");

	race_context.scene.visuals.particle_system.random = *random;

	/*
	 * Rendering has no preceding particle frame to bridge the offline race.
	 */
	race_context.scene.visuals.particle_seconds = field.scene_seconds;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_debug_options debug;
	struct mh_horn_catalog horns;
	struct mh_settings settings;
	struct mh_single_selection selection;
	struct mh_race_session_hooks session;
	struct mh_race_results_handler results;
	struct mh_audio_options audio;
	struct mh_audio *music;
	struct mh_audio_music_state music_state;
	SDL_Window *parent;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	const char *root;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long random;
	unsigned int i, choice;
	unsigned char pixel[4];
	int completed, quit;

	if (mh_debug_parse_arguments(&debug, &argc, argv, NULL) !=
		MH_ERROR_NONE)
		return 1;

	choice = MH_RENDER_CHOICE_GLIDE;

	if (argc < 2 || argc > 3 ||
		(argc == 3 && !mh_render_choice_parse(argv[2], &choice)))
		return 2;

	root = argv[1];
	parent = NULL;
	renderer = NULL;
	texture = NULL;
	random = 1;
	music = NULL;
	status = mh_horn_catalog_load(root, &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			root, &horns, &random, &catalog, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(
			&settings, "Results driver", 0, 0, choice, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	settings.car = 0;
	settings.track = 1;
	settings.laps = 1;
	settings.game_type = 0;
	settings.automatic = 1;
	settings.catch_up = 1;
	random = 0x46e110UL;
	status = mh_single_setup_load(root, &catalog, &horns, &settings, 0,
		&random, &race_context, &field, &selection, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_enable_ai(&field.cars[0],
			&race_context.scene.assets.ai_path, 2, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = finish_race(&random, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		status = mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		goto done;
	}

	parent = SDL_CreateWindow(
		"Retained frontend fixture", 32, 32, SDL_WINDOW_HIDDEN);

	if (parent == NULL) {
		status = mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		goto done;
	}

	renderer = SDL_CreateRenderer(parent, NULL);

	if (renderer == NULL) {
		status = mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		goto done;
	}

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING, 1, 1);

	if (texture == NULL) {
		status = mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		goto done;
	}

	memset(&audio, 0, sizeof(audio));
	memset(&session, 0, sizeof(session));

	for (i = 0; i < field.count; i++)
		audio.horns[i] = selection.profiles[i].horn;

	session.advance = advance;
	session.pause_with_ui = 1;
	results.context = NULL;
	results.commit = accept;

	for (i = 0; i < 8; i++) {
		run_number = i;
		attempts = 0;
		session.league = i == 4;

		if (i == 5 || i == 7) {
			mh_race_context_free(&race_context);
			settings.game_type = i == 5 ? 2 : 0;
			status = mh_single_setup_load(root, &catalog, &horns,
				&settings, 0, &random, &race_context, &field,
				&selection, &error);

			if (status == MH_ERROR_NONE)
				status = mh_drive_enable_ai(&field.cars[0],
					&race_context.scene.assets.ai_path, 2,
					&error);

			if (status == MH_ERROR_NONE)
				status = finish_race(&random, &error);

			if (status != MH_ERROR_NONE)
				goto done;
		}

		if (i == 3 || i == 4 || i == 6) {
			status = mh_field_reset(&field, 1, &error);

			if (status != MH_ERROR_NONE)
				goto done;

			mh_race_scene_skids_reset(&race_context.scene);
		}

		printf("Results fixture: run %u\n", i);
		fflush(stdout);
		printf("Results fixture: intro clock %.17g\n",
			mh_drive_intro_ms(&field.cars[0]));
		fflush(stdout);
		completed_clock = mh_drive_clock_ms(&field.cars[0]);
		completed_ticks = field.cars[0].ticks;
		settings.av.music_volume = i == 0 || i == 3 || i == 7 ? 128 : 0;

		if (!mh_race_run_local(&race_context, &field, root, &audio,
			i == 1 ? NULL : &session, &results, &completed,
			&quit, i == 3 || i == 4 ? &music : NULL, &debug)) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"interactive local results failed");
			goto done;
		}

		if (i == 3 &&
			(music == NULL ||
				!mh_audio_music_inspect(music, &music_state) ||
				music_state.track !=
					(race_context.scene.assets.track
							.cd_track &
						255) ||
				!music_state.background || music_state.failed ||
				music_state.callbacks == 0 ||
				music_state.frame <= 4410)) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"save retry lost continued results music");
			goto done;
		}

		if (i == 4 && music != NULL) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"window close handed off music");
			goto done;
		}

		mh_audio_close(music);
		music = NULL;

		if (completed != (i != 2 && i != 4) ||
			quit != (i == 2 || i == 4) ||
			commits !=
				(i == 0			 ? 1U
						: i <= 2 ? 2U
						: i <= 4 ? 3U
						: i == 5 ? 4U
						: i == 6 ? 5U
							: 6U)) {
			fprintf(stderr,
				"run %u: completed %d, quit %d, commits %u\n",
				i, completed, quit, commits);
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"results completion/quit/commit routing differs");
			goto done;
		}

		/*
		 * A child SDL_Quit would destroy these retained objects.
		 * Exercise both window-system reference lifetime and the
		 * parent's renderer/texture.
		 */
		if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) ||
			SDL_GetWindowID(parent) == 0) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"race tore down the caller's video subsystem");
			goto done;
		}

		pixel[0] = 31;
		pixel[1] = 127;
		pixel[2] = 255;
		pixel[3] = 255;

		if (!SDL_UpdateTexture(texture, NULL, pixel, 4) ||
			!SDL_RenderClear(renderer) ||
			!SDL_RenderTexture(renderer, texture, NULL, NULL) ||
			!SDL_RenderPresent(renderer)) {
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
			goto done;
		}

		printf("Results fixture: returned %u, completed %d, quit %d, commits %u\n",
			i, completed, quit, commits);
		fflush(stdout);

		/*
		 * A real frontend exits on quit. This fixture deliberately
		 * launches further cases, discarding SDL's queued global
		 * last-window quit.
		 */
		if (quit) {
			SDL_PumpEvents();
			SDL_FlushEvent(SDL_EVENT_QUIT);
		}
	}

done:
	mh_audio_close(music);
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Local results fixture: %s\n", error.message);

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(parent);
	SDL_Quit();
	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE;
}
