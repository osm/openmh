#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"

static struct mh_race_context race_context;
static struct mh_field field;
static const SDL_Keycode keys[10] = {SDLK_F, SDLK_M, SDLK_P, SDLK_R, SDLK_E,
	SDLK_Q, SDLK_N, SDLK_HOME, SDLK_B, SDLK_V};

struct binding_fixture {
	unsigned int	warm, phase, action, pulses, quiet, frames;
	Uint64				started;
	double				magnitude;
	SDL_Joystick			*joystick;
	struct mh_control_layout	*layout;
	struct mh_control_binding_set	*bindings;
};

static enum mh_error_code
queue_key(SDL_Keycode code, int down, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"binding fixture has no keyboard focus");

	memset(&event, 0, sizeof(event));
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID(window);
	event.key.key = code;
	event.key.scancode = SDL_GetScancodeFromKey(code, NULL);
	event.key.down = down != 0;

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot queue binding key");
}

static enum mh_error_code
queue_focus(int gained, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"binding fixture has no window");

	memset(&event, 0, sizeof(event));
	event.type = gained ? SDL_EVENT_WINDOW_FOCUS_GAINED
			: SDL_EVENT_WINDOW_FOCUS_LOST;
	event.window.windowID = SDL_GetWindowID(window);

	return SDL_PushEvent(&event)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot queue binding focus change");
}

static enum mh_error_code
queue_mouse(float motion, int left, int right, int middle,
	struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;
	unsigned int i;
	int buttons[3];
	static const Uint8 ids[3] = {
		SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"device fixture has no mouse window");

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = SDL_GetWindowID(window);
	event.motion.xrel = motion;

	if (!SDL_PushEvent(&event))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot queue mouse motion");

	buttons[0] = left;
	buttons[1] = right;
	buttons[2] = middle;

	for (i = 0; i < 3; i++) {
		memset(&event, 0, sizeof(event));
		event.type = buttons[i] ? SDL_EVENT_MOUSE_BUTTON_DOWN
					: SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.windowID = SDL_GetWindowID(window);
		event.button.button = ids[i];

		if (!SDL_PushEvent(&event))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"cannot queue mouse button");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
queue_wheel(float amount, struct mh_error *error_out)
{
	SDL_Event event;
	SDL_Window *window;

	window = SDL_GetKeyboardFocus();

	if (window == NULL)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "missing wheel focus");

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.windowID = SDL_GetWindowID(window);
	event.wheel.y = amount;

	if (!SDL_PushEvent(&event))
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "cannot queue wheel");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance_devices(void *context, const struct mh_engine_input *input,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct binding_fixture *fixture;
	enum mh_error_code status;
	double steering, throttle, brake;
	unsigned int i;
	int paused;

	fixture = (struct binding_fixture *)context;
	*steps = 0;
	*done = 0;
	SDL_Delay(20);

	if (fixture->warm) {
		fixture->warm--;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (fixture->phase == 1)
		steering = -.25;
	else if (fixture->phase == 2)
		steering = .25;
	else if (fixture->phase == 4)
		steering = -(double).2f;
	else if (fixture->phase == 6)
		steering = (double).2f;
	else
		steering = 0;

	if (fixture->phase == 1 || fixture->phase == 12 || fixture->phase == 14)
		throttle = (double).55f;
	else if (fixture->phase == 4)
		throttle = 1;
	else
		throttle = 0;

	if (fixture->phase == 2 || fixture->phase == 13 || fixture->phase == 17)
		brake = (double).55f;
	else if (fixture->phase == 5)
		brake = (double).7f;
	else
		brake = 0;

	paused = fixture->phase == 8 || fixture->phase == 9;

	if (input->steering != steering || input->throttle != throttle ||
		input->brake != brake ||
		input->handbrake !=
			(fixture->phase == 2 || fixture->phase == 4) ||
		input->shift_down != (fixture->phase == 2) ||
		input->shift_up != 0 ||
		field.cars[0].horn != (fixture->phase == 1) ||
		field.cars[0].look_back != (fixture->phase == 2) ||
		(action != MH_FRAME_RUN) != paused) {
		fprintf(stderr,
			"Device phase %u: steering %.9g throttle %.9g brake %.9g hand %d shifts %d/%d horn %d rear %d pause %d\n",
			fixture->phase, input->steering, input->throttle,
			input->brake, input->handbrake, input->shift_up,
			input->shift_down, field.cars[0].horn,
			field.cars[0].look_back, elapsed == 0);
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"device binding did not reach expected race input");
	}

	status = mh_field_advance(
		&field, input, elapsed, action, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (fixture->phase == 0) {
		SDL_SetJoystickVirtualAxis(fixture->joystick, 0, -8192);
		SDL_SetJoystickVirtualAxis(fixture->joystick, 1, -16384);
		SDL_SetJoystickVirtualButton(fixture->joystick, 2, 1);
	} else if (fixture->phase == 1) {
		SDL_SetJoystickVirtualAxis(fixture->joystick, 0, 8192);
		SDL_SetJoystickVirtualAxis(fixture->joystick, 1, 16384);
		SDL_SetJoystickVirtualButton(fixture->joystick, 2, 0);
		SDL_SetJoystickVirtualButton(fixture->joystick, 4, 1);
		SDL_SetJoystickVirtualButton(fixture->joystick, 3, 1);
		SDL_SetJoystickVirtualButton(fixture->joystick, 1, 1);
	} else if (fixture->phase == 2) {
		SDL_SetJoystickVirtualAxis(fixture->joystick, 0, 0);
		SDL_SetJoystickVirtualAxis(fixture->joystick, 1, 0);

		for (i = 0; i < 8; i++)
			SDL_SetJoystickVirtualButton(
				fixture->joystick, (int)i, 0);
	} else if (fixture->phase == 3) {
		fixture->layout->bindings[0] = 0xf9;
		fixture->layout->bindings[1] = 0xfa;
		fixture->layout->bindings[2] = 0xfd;
		fixture->layout->bindings[3] = 0xfe;
		fixture->layout->bindings[7] = 0xff;
		fixture->layout->mouse_speed[0] =
			fixture->layout->mouse_speed[1] = 1;
		status = mh_control_layout_apply(
			fixture->layout, fixture->bindings, error_out);

		if (status == MH_ERROR_NONE)
			status = queue_mouse(-12, 1, 0, 1, error_out);
	} else if (fixture->phase == 4)
		status = queue_mouse(12, 0, 1, 0, error_out);
	else if (fixture->phase == 5)
		status = queue_mouse(0, 0, 0, 0, error_out);
	else if (fixture->phase == 7)
		SDL_SetJoystickVirtualButton(fixture->joystick, 7, 1);
	else if (fixture->phase == 8)
		SDL_SetJoystickVirtualButton(fixture->joystick, 7, 0);
	else if (fixture->phase == 9)
		SDL_SetJoystickVirtualButton(fixture->joystick, 0, 1);
	else if (fixture->phase == 10)
		SDL_SetJoystickVirtualButton(fixture->joystick, 0, 0);
	else if (fixture->phase == 11) {
		fixture->layout->bindings[MH_CONTROL_ACCELERATE] =
			MH_KEY_JOYSTICK_IN;
		fixture->layout->bindings[MH_CONTROL_BRAKE] =
			MH_KEY_JOYSTICK_OUT;
		status = mh_control_layout_apply(
			fixture->layout, fixture->bindings, error_out);
		SDL_SetJoystickVirtualAxis(fixture->joystick, 2, -16384);
	} else if (fixture->phase == 12)
		SDL_SetJoystickVirtualAxis(fixture->joystick, 2, 16384);
	else if (fixture->phase == 13) {
		SDL_SetJoystickVirtualAxis(fixture->joystick, 2, 0);
		fixture->layout->bindings[MH_CONTROL_ACCELERATE] =
			MH_KEY_MOUSE_IN;
		fixture->layout->bindings[MH_CONTROL_BRAKE] = MH_KEY_MOUSE_OUT;
		fixture->layout->mouse_speed[2] = .25f;
		status = mh_control_layout_apply(
			fixture->layout, fixture->bindings, error_out);

		if (status == MH_ERROR_NONE)
			status = queue_wheel(-1, error_out);
	} else if (fixture->phase == 14 || fixture->phase == 15)
		status = queue_wheel(1, error_out);
	else if (fixture->phase == 16) {
		/* Preserve the positive-Z/MouseUp query bug. */
		fixture->layout->bindings[MH_CONTROL_BRAKE] = MH_KEY_MOUSE_UP;
		status = mh_control_layout_apply(
			fixture->layout, fixture->bindings, error_out);
	} else if (fixture->phase == 18)
		*done = 1;

	fixture->phase++;

	return status;
}

static enum mh_error_code
check_input(const struct mh_engine_input *input, int action,
	struct binding_fixture *fixture, struct mh_error *error_out)
{
	double steering, brake, magnitude;

	steering = action == 0 ? -.5 : action == 1 || action == 10 ? .5 : 0;
	brake = action == 3 || action == 13 ? (double).7f : 0;

	if (fixture->phase == 1 &&
		(action == 0 || action == 1 || action == 3)) {
		magnitude = action == 3 ? input->brake
			: action == 0	? -input->steering
					: input->steering;

		if (fixture->frames) {
			if (!(magnitude >= fixture->magnitude &&
				magnitude <= 1))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"held control ramp decreased or exceeded one");

			if (action == 3)
				brake = input->brake;
			else
				steering = input->steering;
		}

		fixture->magnitude = magnitude;
	}

	if (input->throttle != (action == 2 || action == 12 || action == 13) ||
		input->brake != brake || input->steering != steering ||
		input->shift_up != (action == 4) ||
		input->shift_down != (action == 5 || action == 11) ||
		input->handbrake != (action == 7) ||
		field.cars[0].look_back != (action == 8))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"configured key did not reach the expected driving action");

	if (action != 6 && field.cars[0].horn)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"horn leaked after release or UI suppression");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advance_bindings(void *context, const struct mh_engine_input *input,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct binding_fixture *fixture;
	enum mh_error_code status;
	unsigned long ticks;
	int expected, paused;

	fixture = (struct binding_fixture *)context;
	*steps = 0;
	*done = 0;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	SDL_Delay(20);

	if (fixture->warm) {
		fixture->warm--;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	expected = -1;
	paused = fixture->phase == 6 || fixture->phase == 7 ||
		fixture->phase == 11 || fixture->phase == 12;

	if (fixture->phase == 1)
		expected = (int)fixture->action;

	if (fixture->phase == 3)
		expected = 10;

	if (fixture->phase == 10)
		expected = 2;

	if (fixture->phase == 14)
		expected = 11;

	if (fixture->phase == 15)
		expected = 12;

	if (fixture->phase == 16 || fixture->phase == 17)
		expected = 13;

	status = check_input(input, expected, fixture, error_out);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Binding phase %u action %u\n", fixture->phase,
			fixture->action);
		return status;
	}

	if ((action != MH_FRAME_RUN) != paused)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"configured driving key triggered pause or failed to pause");

	/* Original dim output precedes Enter's close action by one frame. */
	if (race_context.scene.visuals.pause_dim !=
		(fixture->phase == 6 || fixture->phase == 7 ||
			fixture->phase == 8 || fixture->phase == 12 ||
			fixture->phase == 13))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"bound game-menu key did not control the pause menu");

	ticks = field.cars[0].ticks;

	if (fixture->phase == 16)
		field.cars[0].vehicle.engine.gear = 0;

	status = mh_field_advance(
		&field, input, elapsed, action, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (fixture->phase == 16 &&
		(*steps == 0 || field.cars[0].input_brake != 0))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"reverse engine did not clear brake input for next frame");

	if (paused && (field.cars[0].ticks != ticks || *steps != 0))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"paused bindings advanced physics");

	if (fixture->phase == 0) {
		fixture->phase = 1;
		return queue_key(keys[0], 1, error_out);
	}

	if (fixture->phase == 1) {
		if (fixture->action == 0 || fixture->action == 1 ||
			fixture->action == 3) {
			if (!fixture->started)
				fixture->started = SDL_GetTicks();

			fixture->frames++;

			if (SDL_GetTicks() - fixture->started < 1200)
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);

			if (fixture->magnitude != 1)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"held control did not ramp to full strength");
		}

		if (fixture->action == 6) {
			if (!fixture->started)
				fixture->started = SDL_GetTicks();

			if (field.cars[0].horn)
				fixture->pulses++;
			else
				fixture->quiet++;

			if (SDL_GetTicks() - fixture->started < 700)
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);

			if (fixture->pulses < 2 || fixture->quiet == 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"configured horn did not use repeat events");
		}

		fixture->phase = 2;
		fixture->frames = 0;
		fixture->started = 0;

		return queue_key(keys[fixture->action], 0, error_out);
	}

	if (fixture->phase == 2) {
		if (++fixture->action < 9) {
			fixture->phase = 1;
			return queue_key(keys[fixture->action], 1, error_out);
		}

		fixture->phase = 3;
		status = queue_key(keys[0], 1, error_out);

		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[1], 1, error_out);
	}

	if (fixture->phase == 3) {
		fixture->phase = 4;
		status = queue_key(keys[0], 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[1], 0, error_out);
	}

	if (fixture->phase == 4) {
		fixture->phase = 5;
		return queue_key(SDLK_ESCAPE, 1, error_out);
	}

	if (fixture->phase == 5) {
		fixture->phase = 6;
		status = queue_key(SDLK_ESCAPE, 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[9], 1, error_out);
	}

	if (fixture->phase == 6) {
		fixture->phase = 7;
		status = queue_key(keys[9], 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[2], 1, error_out);
	}

	if (fixture->phase == 7) {
		fixture->phase = 8;
		return queue_key(SDLK_RETURN, 1, error_out);
	}

	if (fixture->phase == 8) {
		fixture->phase = 9;
		status = queue_key(SDLK_RETURN, 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[2], 0, error_out);
	}

	if (fixture->phase == 9) {
		fixture->phase = 10;
		return queue_key(keys[2], 1, error_out);
	}

	if (fixture->phase == 10) {
		fixture->phase = 11;
		return queue_focus(0, error_out);
	}

	if (fixture->phase == 11) {
		fixture->phase = 12;
		status = queue_focus(1, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[9], 1, error_out);
	}

	if (fixture->phase == 12) {
		fixture->phase = 13;
		status = queue_key(keys[9], 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(SDLK_RETURN, 1, error_out);
	}

	if (fixture->phase == 13) {
		fixture->phase = 14;
		status = queue_key(SDLK_RETURN, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = queue_key(keys[4], 1, error_out);

		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[5], 1, error_out);
	}

	if (fixture->phase == 14) {
		fixture->phase = 15;
		field.cars[0].countdown = 1;
		field.cars[0].start_ticks = 0;
		status = mh_field_reset(&field, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = queue_key(keys[4], 0, error_out);

		if (status == MH_ERROR_NONE)
			status = queue_key(keys[5], 0, error_out);

		if (status == MH_ERROR_NONE)
			status = queue_key(keys[2], 1, error_out);

		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[3], 1, error_out);
	}

	if (fixture->phase == 15) {
		fixture->phase = 16;
		field.cars[0].countdown = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (fixture->phase == 16) {
		fixture->phase = 17;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (fixture->phase == 17) {
		fixture->phase = 18;
		status = queue_key(keys[2], 0, error_out);
		return status != MH_ERROR_NONE
			? status
			: queue_key(keys[3], 0, error_out);
	}

	if (fixture->phase == 18) {
		fixture->phase = 19;
		*done = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_FAILED, "unexpected binding fixture state");
}

int
main(int argc, char *argv[])
{
	static const unsigned long key_codes[10] = {
		0x21, 0x32, 0x19, 0x13, 0x12, 0x10, 0x31, 0xc7, 0x30, 0x2f};

	struct mh_control_layout layout;
	struct mh_control_binding_set bindings;
	struct mh_drive drive;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct binding_fixture fixture;
	SDL_VirtualJoystickDesc description;
	SDL_JoystickID joystick_id;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;
	int success, devices;

	if (argc != 2 && (argc != 3 || strcmp(argv[2], "--devices") != 0)) {
		fprintf(stderr,
			"Usage: mh-bindings-live GAME_ROOT [--devices]\n");
		return 2;
	}

	devices = argc == 3;
	joystick_id = 0;
	memset(&audio, 0, sizeof(audio));
	memset(&fixture, 0, sizeof(fixture));
	fixture.warm = 5;
	memset(&session, 0, sizeof(session));
	session.context = &fixture;
	session.advance = advance_bindings;
	session.pause_with_ui = 1;
	SDL_SetHintWithPriority(
		SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
	status = mh_control_layout_read(
		(const unsigned char *)"", 0, NULL, &layout, &error);

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < 10; i++)
			layout.bindings[i] = key_codes[i];

		if (devices) {
			static const unsigned long device_codes[10] = {0xf3,
				0xf4, 0xf5, 0xf6, 0xf2, 0xf1, 0xf0, 0xee, 0xef,
				0xeb};

			for (i = 0; i < 10; i++)
				layout.bindings[i] = device_codes[i];
		}

		status = mh_control_layout_apply(&layout, &bindings, &error);
	}

	if (devices && status == MH_ERROR_NONE) {
		if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
			status = mh_error_set(
				&error, MH_ERROR_BACKEND, SDL_GetError());
		else {
			SDL_INIT_INTERFACE(&description);
			description.name = "Motorhead driving device fixture";
			description.naxes = 3;
			description.nbuttons = 8;
			joystick_id = SDL_AttachVirtualJoystick(&description);
			fixture.joystick = joystick_id == 0
				? NULL
				: SDL_OpenJoystick(joystick_id);

			if (fixture.joystick == NULL)
				status = mh_error_set(&error, MH_ERROR_BACKEND,
					SDL_GetError());
		}

		session.advance = advance_devices;
		fixture.layout = &layout;
		fixture.bindings = &bindings;
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load(
			argv[1], 1, 1, 1, &race_context, &error);

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
			"Bindings", &race_context.hud.atlas,
			&race_context.results, &error);

	race_context.controls = &bindings;
	success = status == MH_ERROR_NONE &&
		mh_race_run_session(&race_context, &field, argv[1], &audio,
			NULL, &session, NULL);

	if (success && fixture.phase != 19U) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"binding fixture exited early");
		success = 0;
	}

	if (!success)
		fprintf(stderr, "Binding integration: %s\n",
			status != MH_ERROR_NONE ? error.message
						: "live loop failed");

	mh_race_context_free(&race_context);

	if (devices) {
		SDL_CloseJoystick(fixture.joystick);

		if (joystick_id != 0)
			SDL_DetachVirtualJoystick(joystick_id);

		SDL_Quit();
	}

	if (success)
		puts(devices ? "Live device bindings: XYZ steering/pedals, wheel sensitivity/direction quirk, joystick buttons, filtered mouse, handbrake/rear view and joystick pause/resume passed"
			: "Live bindings: ten actions, held steering/brake ramps, direction/shift priority, countdown gate, reverse brake feedback, repeated horn, shortcut conflicts and pause/focus suppression passed");

	return !success;
}
