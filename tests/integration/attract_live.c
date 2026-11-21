/* Supplied game clocks with real SDL event polling/rendering. */

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

static struct mh_profile_catalog profiles;
static struct mh_race_context race_context;
static struct mh_field field;

struct fixture {
	unsigned int	kind, phase, warm;
	double		last_seconds;
};

static enum mh_error_code
push_input(SDL_Keycode code, int down, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"attract fixture has no focus");

	memset(&event, 0, sizeof(event));

	if (code == 0) {
		event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
		event.window.windowID = SDL_GetWindowID(window);
	} else {
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.windowID = SDL_GetWindowID(window);
		event.key.key = code;
		event.key.scancode = SDL_GetScancodeFromKey(code, NULL);
		event.key.down = down != 0;
	}

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot inject attract input");
}

static void
clock_set(double seconds, unsigned long go)
{
	unsigned int i;

	for (i = 0; i < field.count; i++) {
		field.cars[i].frame.active = 1;
		field.cars[i].frame.seconds = seconds;
		field.cars[i].seconds = seconds;
		field.cars[i].frame.time.now_ms = (float)(seconds * 1000);
		field.cars[i].frame.intro_ms = 1000;
		field.cars[i].frame.time.go_ms = go;
	}
}

static enum mh_error_code
focus_event(int focused, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"focus fixture has no window");

	memset(&event, 0, sizeof(event));
	event.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED
			: SDL_EVENT_WINDOW_FOCUS_LOST;
	event.window.windowID = SDL_GetWindowID(window);

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot inject focus event");
}

static enum mh_error_code
advance(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct fixture *state;
	enum mh_error_code status;
	unsigned int entry;

	state = (struct fixture *)context;
	(void)controls;
	*steps = 0;
	*done = 0;

	if (state->warm) {
		state->warm--;
		return mh_field_clock_advance(&field, elapsed, error_out);
	}

	if (field.records_bound || race_context.scores.bound)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"attract opened record storage");

	if (state->kind == 0) {
		entry = field.viewed_entry;

		if (state->phase == 1 &&
			(entry != 0 ||
				field.cars[entry].camera_mode !=
					MH_CAMERA_IN_CAR))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"attract did not force in-car after GO");

		if ((state->phase == 2 || state->phase == 3) &&
			(entry != 1 ||
				field.cars[entry].camera_mode !=
					MH_CAMERA_TRACK ||
				field.listener_mask != 0))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"attract track cycle/listener release repeated or was missing");

		if (state->phase == 4 &&
			(entry != 1 ||
				field.cars[entry].camera_mode !=
					MH_CAMERA_IN_CAR ||
				field.listener_mask != 0))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"in-car transition incorrectly rebound sound");

		if (state->phase == 5 &&
			(entry != 1 ||
				field.cars[entry].camera_mode !=
					MH_CAMERA_CHASE ||
				field.listener_mask != 2))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"attract camera incorrectly overrode the later user command");

		if (state->phase > 5)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"automatic exit advanced physics again");

		status = mh_field_clock_advance(&field, elapsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (state->phase == 0)
			clock_set(13.5, 13000);

		if (state->phase == 1)
			clock_set(23.5, 13000);

		if (state->phase == 2)
			clock_set(24, 13000);

		if (state->phase == 3 || state->phase == 4)
			clock_set(33.5, 13000);

		if (state->phase == 4)
			status = push_input(SDLK_F2, 1, error_out);

		if (state->phase == 5) {
			status = push_input(SDLK_F2, 0, error_out);

			if (status == MH_ERROR_NONE)
				status = push_input(SDLK_RETURN, 1, error_out);

			clock_set(54, 13000);
			state->last_seconds = 54;
		}
	} else if (state->kind == 5) {
		if (state->phase > 2)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"focus fixture did not exit");

		if ((action == MH_FRAME_SUSPEND) != (state->phase == 1))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"attract focus did not suspend/resume");

		status = mh_field_clock_advance(&field, elapsed, error_out);

		if (status == MH_ERROR_NONE)
			status = state->phase == 2
				? push_input(SDLK_RETURN, 1, error_out)
				: focus_event(state->phase == 1, error_out);

		state->last_seconds = field.cars[0].frame.seconds;
	} else if (state->kind == 6) {
		if (state->phase > 1)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"prior finish deadline advanced another frame");

		status = mh_field_clock_advance(&field, elapsed, error_out);
		field.cars[0].race.finished = field.standings.cars[0].finished =
			1;
		clock_set(state->phase == 0 ? 20 : 32, 13000);
		state->last_seconds = field.cars[0].frame.seconds;
	} else if (state->kind == 3) {
		if (state->phase > 2)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"manual attract exit advanced physics again");

		if (state->phase != 0 && action != MH_FRAME_MENU)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"bound menu key did not open attract pause menu");

		status = mh_field_clock_advance(&field, elapsed, error_out);

		if (status == MH_ERROR_NONE)
			status = push_input(
				state->phase == 2 ? SDLK_RETURN : SDLK_V,
				state->phase != 1, error_out);

		state->last_seconds = field.cars[0].frame.seconds;
	} else {
		if (state->phase != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"raw exit/window close did not leave attract mode");

		status = mh_field_clock_advance(&field, elapsed, error_out);

		if (status == MH_ERROR_NONE)
			status = push_input(state->kind == 1 ? SDLK_RETURN
					: state->kind == 2   ? SDLK_ESCAPE
							: 0,
				1, error_out);

		state->last_seconds = field.cars[0].frame.seconds;
	}

	state->phase++;

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_horn_catalog horns;
	struct mh_control_layout layout;
	struct mh_control_binding_set bindings;
	struct mh_settings saved, before, runtime;
	struct mh_single_selection selection;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct fixture state;
	char previous[16];
	struct mh_error error;
	enum mh_error_code status;
	unsigned long random;
	unsigned int kind;
	int success, manual, quit;

	if (argc != 2) {
		fprintf(stderr, "Usage: mh-attract-live GAME_ROOT\n");
		return 2;
	}

	SDL_SetHintWithPriority(
		SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	random = 1;
	previous[0] = 0;
	status = mh_horn_catalog_load(argv[1], &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			argv[1], &horns, &random, &profiles, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(&saved, "Unchanged", 35, 0,
			MH_RENDER_CHOICE_GLIDE, &error);

	if (status == MH_ERROR_NONE)
		status = mh_control_layout_read(
			(const unsigned char *)"", 0, NULL, &layout, &error);

	if (status == MH_ERROR_NONE) {
		layout.bindings[MH_CONTROL_GAME_MENU] = 0x2f;
		status = mh_control_layout_apply(&layout, &bindings, &error);
	}

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Attract fixture setup: %s\n", error.message);
		return 1;
	}

	saved.record_race = 1;
	saved.game_type = 1;
	before = saved;
	memset(&audio, 0, sizeof(audio));
	memset(&session, 0, sizeof(session));
	session.context = &state;
	session.advance = advance;
	session.pause_with_ui = 1;
	success = 1;

	for (kind = 0; kind < 7 && success; kind++) {
		memset(&state, 0, sizeof(state));
		state.kind = kind;
		state.warm = 8;
		status = mh_attract_setup_load(argv[1], &profiles, &horns,
			&saved, previous, 1, &random, &race_context, &field,
			&selection, &runtime, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Attract assets: %s\n", error.message);
			success = 0;
			break;
		}

		race_context.controls = &bindings;
		clock_set(1, 0);
		success = mh_race_run_attract(&race_context, &field, argv[1],
			&audio, &session, &manual, &quit, NULL);

		if (success &&
			(manual != ((kind > 0 && kind < 4) || kind == 5) ||
				quit != (kind == 4) ||
				state.phase !=
					(kind == 0 ? 6U
							: kind == 3 || kind == 5
							? 3U
							: kind == 6 ? 2U
								: 1U) ||
				memcmp(&before, &saved, sizeof(saved)) != 0))
			success = 0;

		if (success && kind != 3 && kind != 4 &&
			!(field.cars[0].frame.seconds > state.last_seconds))
			success = 0;

		if (success && kind == 3 &&
			field.cars[0].frame.seconds != state.last_seconds)
			success = 0;

		if (!success)
			fprintf(stderr,
				"Attract live kind %u, phase %u, manual %d, quit %d, clock %.9f/%.9f\n",
				kind, state.phase, manual, quit,
				field.cars[0].frame.seconds,
				state.last_seconds);

		mh_race_context_free(&race_context);
	}

	if (success)
		puts("Attract live: camera/key order, listener release, timed/Enter/Escape/menu/window/finish exits, focus resume, final clock and settings isolation passed");

	return !success;
}
