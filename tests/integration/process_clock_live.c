#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static struct mh_race_context race_context;
static struct mh_field field;

struct clock_fixture {
	unsigned int		phase, frames;
	struct mh_drive_frame	previous;
};

static enum mh_error_code
check_real_clock(struct mh_error *error_out)
{
	long double game, before, after;
	unsigned long first, word, last;
	enum mh_error_code status;

	game = mh_game_clock_ms();
	before = mh_game_clock_real_ms();
	status = mh_game_clock_real_word(&word, error_out);
	after = mh_game_clock_real_ms();

	if (status == MH_ERROR_NONE)
		status = mh_clock_word(before, &first, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_clock_word(after, &last, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (after < before ||
		((word - first) & MH_U32_MAX) > ((last - first) & MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"absolute clock word is outside its surrounding counter samples");

	if (game != mh_game_clock_ms())
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"absolute getter changed accumulated game time");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance_clock(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct clock_fixture *fixture;
	struct mh_drive_frame preview;
	enum mh_error_code status;

	fixture = (struct clock_fixture *)context;

	if (fixture->frames == 0) {
		if (!field.cars[0].frame.active ||
			field.cars[0].frame.pending ||
			field.cars[0].frame.time.now_ms !=
				fixture->previous.time.now_ms ||
			field.cars[0].frame.time.elapsed !=
				fixture->previous.time.elapsed ||
			field.cars[0].frame.intro_ms !=
				(unsigned long)((long double)field.cars[0]
							.frame.seconds *
					1000.0L))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"live race load replaced retained clock products");

		if (fixture->phase == 1 &&
			field.cars[0].frame.seconds <
				fixture->previous.seconds + .060)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"menu/loading time did not reach the next race");
	}

	status = mh_drive_frame_sample(
		&field.cars[0], elapsed, &preview, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_advance(
			&field, controls, elapsed, action, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (preview.seconds != field.cars[0].frame.seconds ||
		memcmp(&preview.time, &field.cars[0].frame.time,
			sizeof(preview.time)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"live process clock differs from the frame preview");

	fixture->frames++;
	*done = fixture->frames == 4;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_drive drive;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct clock_fixture fixture;
	struct mh_error error;
	enum mh_error_code status;
	long double milliseconds, real_before;

	if (argc != 2)
		return 2;

	SDL_SetHintWithPriority(
		SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	status = check_real_clock(&error);

	if (status == MH_ERROR_NONE)
		status = mh_game_clock_start(&error);

	real_before = mh_game_clock_real_ms();
	milliseconds = mh_game_clock_ms();
	SDL_Delay(10);

	if (status == MH_ERROR_NONE && mh_game_clock_ms() != milliseconds)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"reading the game clock sampled wall time");

	if (status == MH_ERROR_NONE && mh_game_clock_real_ms() <= real_before)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"absolute clock stopped with the game clock");

	if (status == MH_ERROR_NONE)
		status = check_real_clock(&error);

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load(
			argv[1], 1, 1, 1, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_load(argv[1], &race_context.hud, &error);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load(argv[1], 1, "Clock fixture",
			&race_context.hud.atlas, &race_context.results, &error);

	memset(&fixture, 0, sizeof(fixture));
	memset(&audio, 0, sizeof(audio));
	memset(&session, 0, sizeof(session));
	session.context = &fixture;
	session.pause_with_ui = 1;
	session.advance = advance_clock;

	for (fixture.phase = 0; fixture.phase < 3 && status == MH_ERROR_NONE;
		fixture.phase++) {
		if (fixture.phase != 0)
			SDL_Delay(75);

		if (fixture.phase == 2) {
			real_before = mh_game_clock_real_ms();
			status = mh_game_clock_reset(&error);

			if (status == MH_ERROR_NONE &&
				mh_game_clock_real_ms() < real_before)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"lobby reset rewound the absolute clock");
		}

		if (status == MH_ERROR_NONE)
			status = check_real_clock(&error);

		if (status == MH_ERROR_NONE)
			status = mh_drive_start(&race_context.scene.assets.car,
				&race_context.scene.assets.world_collision,
				&race_context.scene.assets.materials,
				&race_context.scene.assets.route,
				&race_context.scene.assets.track, 0, 0, 1,
				&drive, &error);

		if (status == MH_ERROR_NONE)
			status = mh_field_start(
				&drive, NULL, 0, NULL, 0, &field, &error);

		if (status == MH_ERROR_NONE)
			status = mh_field_set_hulls(&field,
				race_context.scene.assets.car_hulls,
				field.count, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_bind_objects(
				&race_context.scene, &field, &error);

		fixture.frames = 0;

		if (status == MH_ERROR_NONE &&
			!mh_race_run_session(&race_context, &field, argv[1],
				&audio, NULL, &session, NULL))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"live process clock fixture failed");

		if (status == MH_ERROR_NONE && fixture.frames != 4)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"live process clock fixture ended early");

		if (status == MH_ERROR_NONE &&
			mh_game_clock_ms() !=
				(long double)field.cars[0].frame.seconds *
					1000.0L)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"returned race clock is not available to lobby notifications");

		fixture.previous = field.cars[0].frame;
	}

	mh_race_context_free(&race_context);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);
	else
		puts("Live process clock: three race entries retain float history, menu/load duration and lobby reset sampling; absolute clock stays independent");

	return status != MH_ERROR_NONE;
}
