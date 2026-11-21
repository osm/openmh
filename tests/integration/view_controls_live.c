#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"

static struct mh_race_context race_context;
static struct mh_field field;

struct fixture {
	unsigned int	phase, warm, pulses, quiet, viewed;
	unsigned long	revision;
	Uint64		started;
};

static enum mh_error_code
key_event(SDL_Keycode key, int down, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"camera fixture has no keyboard focus");

	memset(&event, 0, sizeof(event));
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID(window);
	event.key.key = key;
	event.key.scancode = SDL_GetScancodeFromKey(key, NULL);
	event.key.down = down != 0;

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot queue camera input");
}

static enum mh_error_code
advance(void *context, const struct mh_engine_input *input, long double elapsed,
	enum mh_frame_action action, unsigned int *steps, int *done,
	struct mh_error *error_out)
{
	struct fixture *state;
	struct mh_drive *viewed;
	enum mh_camera_mode expected;
	unsigned long changes;
	enum mh_error_code status;
	int paused;

	state = (struct fixture *)context;
	*steps = 0;
	*done = 0;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	(void)elapsed;
	SDL_Delay(20);

	if (state->warm) {
		state->warm--;
		state->revision = field.listener_revision;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	viewed = &field.cars[mh_field_viewed(&field)];
	paused = state->phase == 14 || state->phase == 15;

	if ((action == MH_FRAME_MENU) != paused || input->throttle ||
		input->brake || input->steering)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"camera binding triggered a native pause shortcut or driving action");

	expected = state->phase < 4			  ? MH_CAMERA_CHASE
		: state->phase >= 7 && state->phase <= 11 ? MH_CAMERA_TRACK
							: MH_CAMERA_IN_CAR;
	changes = state->phase == 4 || state->phase == 5 || state->phase == 9 ||
			state->phase == 12 || state->phase == 17
		? 1
		: state->phase == 7 ? 2
		: state->phase == 8 ? 3
				: 0;

	if (viewed->camera_mode != expected ||
		mh_field_listener_positional(&field) !=
			(expected == MH_CAMERA_TRACK))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"camera binding or listener mode is incorrect");

	if (state->phase != 10 &&
		field.listener_revision != state->revision + changes)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"camera request order/held behavior changed listener command count");

	if (viewed->look_back != (state->phase == 12))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"configured rear view did not follow the viewed car");

	if (state->phase == 9 && field.viewed_entry != 1)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"cycle binding did not select next member");

	if (state->phase == 10) {
		if (field.listener_revision == state->revision) {
			if (field.viewed_entry != state->viewed)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"view changed without a cycle event");

			state->quiet++;
		} else if (field.listener_revision == state->revision + 1) {
			if (field.viewed_entry !=
				(state->viewed + 1) % field.count)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"repeated cycle selected wrong member");

			state->pulses++;
		} else
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held cycling produced too many commands");

		state->revision = field.listener_revision;
		state->viewed = field.viewed_entry;

		if (SDL_GetTicks() - state->started < 650)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (state->pulses < 2 || state->quiet == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held cycle did not use keyboard repeat events");
	}

	state->revision = field.listener_revision;
	state->viewed = field.viewed_entry;

	if (state->phase == 0)
		status = key_event(SDLK_F1, 1, error_out);
	else if (state->phase == 1) {
		status = key_event(SDLK_F1, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_P, 1, error_out);

		/*
		 * Leave room for the next real SDL frame interval on either
		 * side of six seconds. The isolated original query covers exact
		 * boundaries.
		 */
	} else if (state->phase == 2 || state->phase == 3) {
		field.cars[0].frame.seconds =
			field.cars[0].frame.intro_ms * .001 +
			(state->phase == 2 ? 5 : 7);
	} else if (state->phase == 5)
		status = key_event(SDLK_P, 0, error_out);
	else if (state->phase == 6) {
		status = key_event(SDLK_F, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_G, 1, error_out);
	} else if (state->phase == 7)
		status = key_event(SDLK_P, 1, error_out);
	else if (state->phase == 8) {
		status = key_event(SDLK_P, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_F, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_G, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_N, 1, error_out);
	} else if (state->phase == 9)
		state->started = SDL_GetTicks();
	else if (state->phase == 10)
		status = key_event(SDLK_N, 0, error_out);
	else if (state->phase == 11) {
		status = key_event(SDLK_P, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_B, 1, error_out);
	} else if (state->phase == 12) {
		status = key_event(SDLK_P, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_B, 0, error_out);
	} else if (state->phase == 13)
		status = key_event(SDLK_V, 1, error_out);
	else if (state->phase == 14) {
		status = key_event(SDLK_V, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_G, 1, error_out);
	} else if (state->phase == 15) {
		status = key_event(SDLK_G, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_RETURN, 1, error_out);
	} else if (state->phase == 16) {
		status = key_event(SDLK_RETURN, 0, error_out);
		viewed->camera_goal = 1;

		if (status == MH_ERROR_NONE)
			status = key_event(SDLK_P, 1, error_out);
	} else if (state->phase == 17) {
		if (viewed->camera_goal)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"held in-car command did not clear finish camera");

		status = key_event(SDLK_P, 0, error_out);
	} else if (state->phase == 18)
		*done = 1;

	state->phase++;

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Camera fixture phase %u\n", state->phase - 1);

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_control_layout layout;
	struct mh_control_binding_set bindings;
	struct mh_drive drive;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct fixture fixture;
	const struct mh_car *roster[2];
	struct mh_error error;
	enum mh_error_code status;
	int success;

	if (argc != 2) {
		fprintf(stderr, "Usage: mh-view-controls-live GAME_ROOT\n");
		return 2;
	}

	memset(&audio, 0, sizeof(audio));
	memset(&fixture, 0, sizeof(fixture));
	fixture.warm = 5;
	memset(&session, 0, sizeof(session));
	session.context = &fixture;
	session.advance = advance;
	session.pause_with_ui = 1;
	SDL_SetHintWithPriority(
		SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	status = mh_control_layout_read(
		(const unsigned char *)"", 0, NULL, &layout, &error);

	if (status == MH_ERROR_NONE) {
		layout.bindings[MH_CONTROL_IN_CAR] = 0x19;
		layout.bindings[MH_CONTROL_OUT_CAR] = 0x21;
		layout.bindings[MH_CONTROL_CAMERA] = 0x22;
		layout.bindings[MH_CONTROL_CYCLE] = 0x31;
		layout.bindings[MH_CONTROL_REAR_VIEW] = 0x30;
		layout.bindings[MH_CONTROL_GAME_MENU] = 0x2f;
		status = mh_control_layout_apply(&layout, &bindings, &error);
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load(
			argv[1], 1, 1, 1, &race_context, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai(
			argv[1], &race_context.scene, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_add_opponents(
			argv[1], 1, 1, 2, &race_context.scene, &error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 1, &drive,
			&error);

	roster[0] = &race_context.scene.assets.opponents[0];
	roster[1] = &race_context.scene.assets.opponents[1];

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, roster, 2,
			&race_context.scene.assets.ai_path, 1, &field, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, field.count,
			&error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&race_context.scene, &field, &error);

	if (status == MH_ERROR_NONE) {
		/*
		 * Bind the original initial in-car preference, then
		 * deliberately choose chase for this input fixture before
		 * entering the renderer.
		 */
		mh_race_scene_bind_cameras(&race_context.scene, &field);
		status = mh_field_camera_mode(&field, MH_CAMERA_CHASE, &error);
	}

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_load(argv[1], &race_context.hud, &error);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load(argv[1], field.count, "Views",
			&race_context.hud.atlas, &race_context.results, &error);

	race_context.controls = &bindings;
	success = status == MH_ERROR_NONE &&
		mh_race_run_session(&race_context, &field, argv[1], &audio,
			NULL, &session, NULL);

	if (success && fixture.phase != 19) {
		status = mh_error_set(
			&error, MH_ERROR_FAILED, "camera fixture exited early");
		success = 0;
	}

	if (!success)
		fprintf(stderr, "Camera integration phase %u: %s\n",
			fixture.phase,
			status != MH_ERROR_NONE ? error.message
						: "live loop failed");

	mh_race_context_free(&race_context);

	if (success)
		puts("Live camera bindings: remapping, held views, ordered simultaneous requests, sampled six-second gate, repeated member cycling, listener ownership, rear view and pause suppression passed");

	return !success;
}
