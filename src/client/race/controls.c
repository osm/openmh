#include <string.h>

#include "client/race/internal.h"
#include "engine/core/error.h"

static enum mh_error_code
binding_actions(const struct mh_control_binding_set *bindings,
	const struct mh_keyboard *keyboard,
	const struct mh_device_input *devices, struct mh_binding_input *input,
	unsigned int active[MH_RACE_ACTION_COUNT], struct mh_error *error_out)
{
	static const unsigned int slots[] = {
		MH_CONTROL_HORN, MH_CONTROL_GAME_MENU};

	enum mh_error_code status;
	unsigned int i;

	memset(active, 0, MH_RACE_ACTION_COUNT * sizeof(active[0]));
	memset(input, 0, sizeof(*input));

	for (i = 0; i < MH_KEY_COUNT; i++) {
		input->down[i] = keyboard->held[i] ? MH_KEY_DOWN_BIT : 0;
		input->events[i] = keyboard->events[i];
	}

	if (bindings == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < MH_DEVICE_AXES; i++) {
		input->mouse_speed[i] = bindings->mouse_speed[i];
		input->mouse[i] = devices->mouse[i];
		input->joystick[i] = devices->joystick[i];
		input->minimum[i] = devices->minimum[i];
		input->maximum[i] = devices->maximum[i];
	}

	for (i = 0; i < MH_MOUSE_BUTTONS; i++)
		if (devices->mouse_buttons[i])
			input->mouse_buttons |= 1U << i;

	for (i = 0; i < MH_JOYSTICK_BUTTONS; i++)
		if (devices->joystick_buttons[i])
			input->joystick_buttons |= 1U << i;

	for (i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
		status = mh_binding_query(input, bindings->codes[slots[i]], 1,
			&active[slots[i]], NULL, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_race_poll_input(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (loop->drive->frame.pending && !loop->tick_clock.rate)
		loop->wall_elapsed = 0;

	status = mh_game_timer_real_word(
		loop->timer, &loop->input_clock, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_input_poll(&loop->devices, &loop->keyboard,
			loop->down, loop->input_clock,
			loop->screen.visible ? MH_DEVICE_KEYS_MENU
					: loop->input_mode,
			&loop->device_input, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Keyboard: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	/*
	 * Service CD audio after keyboard polling and before the pause/settings
	 * consumer. CDLoop uses real time, even while the menu stops physics.
	 */
	if (loop->capture == NULL && loop->focused && !loop->screen.prepared &&
		(loop->continuous ||
			(!loop->paused &&
				!mh_results_screen_pauses(&loop->screen,
					&loop->field->standings))) &&
		!mh_audio_music_poll(
			loop->audio, mh_game_timer_real_ms(loop->timer))) {
		mh_audio_close(loop->audio);
		loop->audio = NULL;
	}

	status = binding_actions(loop->context->controls, &loop->keyboard,
		&loop->device_input, &loop->binding_input, loop->actions,
		&loop->error);
	loop->info_held = 0;

	if (status == MH_ERROR_NONE && loop->focused && loop->capture == NULL)
		status = mh_binding_query(&loop->binding_input,
			loop->context->controls != NULL
				? loop->context->controls
					->codes[MH_CONTROL_INFO]
				: (MH_BINDING_KEYBOARD | MH_KEY_TAB),
			0, &loop->info_held, NULL, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Control bindings: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	if (loop->focused && loop->capture == NULL && loop->screen.visible &&
		(loop->keyboard.events[MH_KEY_ENTER] ||
			loop->keyboard.events[MH_KEY_ESCAPE])) {
		if (loop->result_handler != NULL && loop->screen.prepared) {
			if (loop->results_ready)
				loop->accept_results = 1;
		} else {
			loop->dismissed_results = 1;
			loop->screen.visible = 0;
		}
	}

	return 1;
}

int
mh_race_update_chat(struct mh_race_loop *loop)
{
	enum mh_error_code status;
	unsigned int i;

	if (loop->chat_enabled && loop->focused && loop->capture == NULL &&
		!loop->paused && !loop->menu.visible && !loop->screen.visible) {
		mh_input_characters(loop->inputs, loop->down, loop->characters);
		status = mh_chat_step(loop->session->chat,
			loop->session->chat_config,
			loop->context->controls != NULL
				? loop->context->controls
				: &loop->chat_defaults,
			&loop->binding_input, loop->characters,
			loop->context->hud.message_advance, &loop->chat_output,
			&loop->error);

		for (i = 0;
			status == MH_ERROR_NONE && i < loop->chat_output.count;
			i++)
			status = loop->session->send_chat(
				loop->session->context,
				loop->chat_output.sends[i].text,
				loop->chat_output.sends[i].color,
				(unsigned long)((long double)loop->input_frame
							.seconds *
					1000.0L),
				&loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Chat: %s\n", loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	return 1;
}

static enum mh_error_code
read_bound_driving(struct mh_race_loop *loop,
	const struct mh_binding_input *input, int chat,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_drive_controls_step(loop->context->controls, input,
		&loop->shaped_controls, loop->control_seconds,
		loop->control_racing, 0, chat, &loop->shaped_controls,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	loop->controls.throttle = loop->shaped_controls.throttle;
	loop->controls.brake = loop->shaped_controls.brake;
	loop->controls.steering = loop->shaped_controls.steering;
	loop->controls.handbrake = loop->shaped_controls.handbrake != 0;
	loop->controls.shift_up = (loop->shaped_controls.command &
					MH_DRIVE_COMMAND_SHIFT_UP) != 0;
	loop->controls.shift_down = (loop->shaped_controls.command &
					MH_DRIVE_COMMAND_SHIFT_DOWN) != 0;

	return status;
}

static void
read_default_driving(struct mh_race_loop *loop)
{
	/* Match the shipped Keyboard profile when no CLO is loaded. */
	loop->controls.throttle = mh_input_key_down(loop->inputs, MH_KEY_UP);
	loop->controls.brake = mh_input_key_down(loop->inputs, MH_KEY_DOWN);
	loop->controls.steering =
		mh_input_key_down(loop->inputs, MH_KEY_RIGHT) -
		mh_input_key_down(loop->inputs, MH_KEY_LEFT);
	loop->controls.handbrake =
		mh_input_key_down(loop->inputs, MH_KEY_LSHIFT) ||
		mh_input_key_down(loop->inputs, MH_KEY_RSHIFT);
	loop->controls.shift_up = mh_input_key_down(loop->inputs, MH_KEY_S);
	loop->controls.shift_down = mh_input_key_down(loop->inputs, MH_KEY_A);
}

int
mh_race_read_driving_controls(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	memset(&loop->controls, 0, sizeof(loop->controls));

	if (loop->focused && !loop->paused && !loop->menu.visible &&
		!loop->screen.visible) {
		if (loop->context->controls != NULL) {
			status = read_bound_driving(loop, &loop->binding_input,
				loop->chat_block, &loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Driving controls: %s\n",
					loop->error.message);
				loop->success = 0;
				loop->running = 0;
				return 0;
			}
		} else if (!loop->chat_block) {
			read_default_driving(loop);
		}
	} else
		memset(&loop->shaped_controls, 0,
			sizeof(loop->shaped_controls));

	return 1;
}

static enum mh_error_code
sample_shift_binding(struct mh_tick_input_sample *sample, unsigned long code,
	unsigned int shift, struct mh_error *error_out)
{
	unsigned int active;
	enum mh_error_code status;

	status = mh_binding_query(
		&sample->binding, code, 0, &active, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active)
		sample->shift_held |= shift;

	status = mh_binding_query(
		&sample->binding, code, 1, &active, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (active)
		sample->shift_pressed |= shift;

	return status;
}

static enum mh_error_code
sample_tick_buttons(const struct mh_race_loop *loop,
	struct mh_tick_input_sample *sample, struct mh_error *error_out)
{
	unsigned int active;
	enum mh_error_code status;

	status = sample_shift_binding(sample,
		loop->context->controls->codes[MH_CONTROL_SHIFT_UP],
		MH_TICK_SHIFT_UP, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = sample_shift_binding(sample,
		loop->context->controls->codes[MH_CONTROL_SHIFT_DOWN],
		MH_TICK_SHIFT_DOWN, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_binding_query(&sample->binding,
		loop->context->controls->codes[MH_CONTROL_HORN], 0, &active,
		NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sample->horn = active != 0;

	return status;
}

enum mh_error_code
mh_race_queue_tick_input(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_tick_input_sample sample;
	enum mh_error_code status;

	memset(&sample, 0, sizeof(sample));
	sample.enabled = loop->focused && !loop->paused &&
		!loop->menu.visible && !loop->screen.visible &&
		!loop->chat_block;
	sample.binding = loop->binding_input;
	status = MH_ERROR_NONE;

	if (sample.enabled && loop->context->controls != NULL) {
		status = sample_tick_buttons(loop, &sample, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (sample.enabled) {
		read_default_driving(loop);
		sample.fallback = loop->controls;
		sample.shift_held =
			(loop->controls.shift_up ? MH_TICK_SHIFT_UP : 0) |
			(loop->controls.shift_down ? MH_TICK_SHIFT_DOWN : 0);
		sample.horn =
			mh_input_key_down(loop->inputs, MH_KEY_SPACE) != 0;
	}

	status = mh_tick_input_write(&loop->tick_input,
		mh_tick_clock_input_tick(&loop->tick_clock), &sample,
		error_out);

	return status;
}

enum mh_error_code
mh_race_read_tick_input(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_tick_input_sample sample;
	unsigned int shifts;
	enum mh_error_code status;

	status = mh_tick_input_read(&loop->tick_input,
		loop->tick_clock.ticks + 1, &sample, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_drive_frame_sample(
		loop->drive, loop->elapsed, &loop->input_frame, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	loop->control_seconds = (float)loop->elapsed;
	loop->control_racing = loop->input_frame.time.go_ms != 0;
	memset(&loop->controls, 0, sizeof(loop->controls));

	if (!sample.enabled) {
		memset(&loop->shaped_controls, 0,
			sizeof(loop->shaped_controls));
	} else if (loop->context->controls != NULL) {
		status =
			read_bound_driving(loop, &sample.binding, 0, error_out);
	} else
		loop->controls = sample.fallback;

	if (sample.enabled) {
		shifts = sample.shift_held | sample.shift_pressed;
		loop->controls.shift_down = (shifts & MH_TICK_SHIFT_DOWN) != 0;
		loop->controls.shift_up = (shifts & MH_TICK_SHIFT_UP) != 0 &&
			!loop->controls.shift_down;
	}

	if (!loop->drive->remote)
		loop->drive->horn = sample.enabled && sample.horn;

	return status;
}

static enum mh_error_code
apply_bound_views(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_view_controls_query(loop->context->controls,
		&loop->binding_input, loop->view_clock, 0, loop->chat_block,
		(unsigned int)loop->viewed->look_back, &loop->view_controls,
		error_out);

	if (status == MH_ERROR_NONE &&
		(loop->view_controls.requests & MH_VIEW_IN_CAR))
		status = mh_field_camera_mode(
			loop->field, MH_CAMERA_IN_CAR, error_out);

	if (status == MH_ERROR_NONE &&
		(loop->view_controls.requests & MH_VIEW_OUT_CAR))
		status = mh_field_camera_mode(
			loop->field, MH_CAMERA_CHASE, error_out);

	if (status == MH_ERROR_NONE &&
		(loop->view_controls.requests & MH_VIEW_TRACK))
		status = mh_field_camera_mode(
			loop->field, MH_CAMERA_TRACK, error_out);

	if (status == MH_ERROR_NONE)
		loop->viewed->look_back = (int)loop->view_controls.look_back;

	if (status == MH_ERROR_NONE &&
		(loop->view_controls.requests & MH_VIEW_CYCLE))
		status = mh_field_cycle_view(loop->field, error_out);

	return status;
}

int
mh_race_apply_view_controls(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (loop->session != NULL && loop->session->benchmark) {
		loop->viewed->look_back = 0;
		return 1;
	}

	if (loop->capture == NULL && loop->focused && !loop->paused &&
		!loop->menu.visible && !loop->screen.visible) {
		if (loop->context->controls != NULL) {
			status = apply_bound_views(loop, &loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Camera controls: %s\n",
					loop->error.message);
				loop->success = 0;
				loop->running = 0;
				return 0;
			}

			loop->viewed =
				&loop->field
					->cars[mh_field_viewed(loop->field)];
		} else if (!loop->chat_block)
			loop->viewed->look_back =
				mh_input_key_down(loop->inputs, MH_KEY_LCTRL) ||
				mh_input_key_down(loop->inputs, MH_KEY_RCTRL);
	} else if (loop->capture == NULL && loop->continuous)
		loop->viewed->look_back = 0;

	return 1;
}
