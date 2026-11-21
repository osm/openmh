#include "client/race/internal.h"
#include "engine/core/error.h"
#include "engine/render/scene_interpolation.h"

#define DEFAULT_SCREENSHOT_BINDING 0x258UL
#define SCREENSHOT_NOTICE_COLOR 0xe06820UL

static struct mh_screenshot race_screenshot_state;

static int
sound_suspended(const struct mh_race_loop *loop)
{
	return !loop->focused ||
		(!loop->continuous && !loop->menu.visible &&
			!loop->screen.prepared &&
			(loop->paused ||
				(loop->screen.visible &&
					!loop->drive->race.finished)));
}

static void
begin_frame_sound(struct mh_race_loop *loop)
{
	/*
	 * Consume the preceding vehicle/projection state. HUD cues below
	 * consume this frame's updated race progress.
	 */
	if (!mh_audio_frame_begin(loop->audio, loop->field,
		loop->drive->sound_pedal, sound_suspended(loop),
		loop->screen.visible)) {
		mh_audio_close(loop->audio);
		loop->audio = NULL;
	}

	/*
	 * Horn input is published during vehicle preparation, after sound.
	 * Publish before network snapshots, including release while focus/UI
	 * suppression leaves a live session running.
	 */
	if (!loop->drive->remote && loop->tick_clock.rate == 0)
		loop->drive->horn = loop->capture == NULL && loop->focused &&
			!loop->paused && !loop->menu.visible &&
			!loop->screen.visible && !loop->chat_block &&
			(loop->context->controls != NULL
					? loop->actions[MH_CONTROL_HORN]
					: loop->keyboard.events[MH_KEY_SPACE]);
}

static void
update_sound_positions(struct mh_race_loop *loop)
{
	if (!mh_audio_positions(loop->audio, loop->field)) {
		mh_audio_close(loop->audio);
		loop->audio = NULL;
	}
}

static void
end_frame_sound(struct mh_race_loop *loop)
{
	if (!mh_audio_frame_end(loop->audio, loop->field, sound_suspended(loop),
		loop->screen.visible, loop->gap_mode, loop->info_held != 0,
		loop->messages.sound)) {
		mh_audio_close(loop->audio);
		loop->audio = NULL;
	}
}

static int
fail_fixed_tick(struct mh_race_loop *loop)
{
	fprintf(stderr, "Simulation tick: %s\n", loop->error.message);
	loop->in_tick = 0;
	loop->success = loop->running = 0;

	return 0;
}

static int
advance_fixed_tick(struct mh_race_loop *loop)
{
	begin_frame_sound(loop);
	mh_race_feedback_frame(loop);

	if (!mh_race_remember_chase_camera(loop))
		return fail_fixed_tick(loop);

	loop->frame_updated = 1;
	loop->previous_field = *loop->field;
	loop->presentation_history = 1;

	if (!mh_race_advance_simulation(loop)) {
		loop->in_tick = 0;
		return 0;
	}

	if (loop->error.code != MH_ERROR_NONE)
		return fail_fixed_tick(loop);

	if (!mh_race_update_tyre_effects(loop) ||
		!mh_race_update_chase_camera(loop))
		return fail_fixed_tick(loop);

	update_sound_positions(loop);
	end_frame_sound(loop);
	loop->messages.sound = 0;
	mh_tick_clock_commit(&loop->tick_clock, NULL);

	return 1;
}

static int
run_fixed_ticks(struct mh_race_loop *loop)
{
	unsigned int i, due;
	int suspended;
	enum mh_error_code status;

	suspended = loop->frame_request.action != MH_FRAME_RUN ||
		loop->capture != NULL;
	loop->tick_restarted = 0;

	if (loop->session != NULL && loop->session->benchmark)
		loop->elapsed = 1.0L / loop->tick_clock.rate;

	status = mh_tick_clock_add(
		&loop->tick_clock, loop->elapsed, suspended, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_race_queue_tick_input(loop, &loop->error);

	if (status != MH_ERROR_NONE)
		return fail_fixed_tick(loop);

	due = suspended ? 0 : mh_tick_clock_due(&loop->tick_clock);
	loop->frame_updated = 0;
	loop->elapsed = 1.0L / loop->tick_clock.rate;

	for (i = 0; i < due; i++) {
		loop->in_tick = 1;

		if (!mh_race_update_finish(loop))
			return fail_fixed_tick(loop);

		if (!loop->continuous &&
			mh_results_screen_pauses(
				&loop->screen, &loop->field->standings))
			break;

		status = mh_race_read_tick_input(loop, &loop->error);

		if (status != MH_ERROR_NONE)
			return fail_fixed_tick(loop);

		if (loop->tick_restarted)
			break;

		if (!mh_race_update_attract(loop))
			return fail_fixed_tick(loop);

		if (!advance_fixed_tick(loop))
			return 0;
	}

	loop->in_tick = 0;

	if (suspended)
		begin_frame_sound(loop);

	return 1;
}

static int
present_field(struct mh_race_loop *loop)
{
	double alpha;

	if (!loop->tick_clock.rate || !loop->presentation_history)
		return 1;

	alpha = loop->frame_request.action == MH_FRAME_RUN
		? mh_tick_clock_alpha(&loop->tick_clock)
		: 1;

	if (mh_scene_interpolate(&loop->previous_field,
		loop->authoritative_field, alpha, &loop->presented_field,
		&loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Presentation: %s\n", loop->error.message);
		loop->success = loop->running = 0;
		return 0;
	}

	loop->field = &loop->presented_field;
	loop->drive = &loop->field->cars[0];
	loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];

	return 1;
}

static void
restore_field(struct mh_race_loop *loop)
{
	loop->field = loop->authoritative_field;
	loop->drive = &loop->field->cars[0];
	loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];
}

static int
capture_frame(struct mh_race_loop *loop)
{
	/*
	 * Present a board before accepting it, and require the key which
	 * selected pause-menu Quit to be released.
	 */
	if (loop->screen.prepared && !loop->down[MH_KEY_ENTER] &&
		!loop->down[MH_KEY_ESCAPE])
		loop->results_ready = 1;

	if (loop->capture != NULL) {
		loop->success =
			mh_renderer_capture(loop->renderer, loop->capture);

		if (!loop->success) {
			mh_renderer_error(loop->renderer, &loop->error);
			fprintf(stderr, "Capture: %s\n", loop->error.message);
		}

		loop->running = 0;

		return 0;
	}

	return 1;
}

static enum mh_error_code
publish_screenshot_notice(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (loop->session != NULL && loop->session->message != NULL)
		return loop->session->message(loop->session->context,
			"Saved picture: ", loop->screenshot_result.name,
			SCREENSHOT_NOTICE_COLOR, error_out);

	status = mh_clock_word(mh_drive_clock_ms(&loop->field->cars[0]),
		&loop->screenshot_clock, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_messages_add(&loop->local_messages,
			"Saved picture: ", loop->screenshot_result.name,
			SCREENSHOT_NOTICE_COLOR, loop->screenshot_clock,
			loop->context->hud.message_advance, error_out);

	if (status == MH_ERROR_NONE)
		loop->local_messages_present = 1;

	return status;
}

static void
poll_screenshot(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (!loop->focused || loop->screenshot.user_data == NULL) {
		race_screenshot_state.latched = 0;
		return;
	}

	status = mh_screenshot_capture_poll(&loop->screenshot,
		&race_screenshot_state,
		loop->context->controls != NULL
			? loop->context->controls->codes[MH_CONTROL_SCREENSHOT]
			: DEFAULT_SCREENSHOT_BINDING,
		&loop->binding_input, &loop->screenshot_result, &loop->error);

	if (status != MH_ERROR_NONE) {
		if (!loop->screenshot_failed)
			fprintf(stderr, "Screenshot: %s\n",
				loop->error.message);

		loop->screenshot_failed = 1;

		return;
	}

	loop->screenshot_failed = 0;

	if (!loop->screenshot_result.saved)
		return;

	printf("Saved picture: %s/%s\n", loop->screenshot.directory,
		loop->screenshot_result.name);
	fflush(stdout);
	status = publish_screenshot_notice(loop, &loop->error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Screenshot notice: %s\n", loop->error.message);
}

/* Input and UI are sampled once per rendered frame, before tick catch-up. */
static int
prepare_frame(struct mh_race_loop *loop)
{
	if (!mh_race_sample_frame_clock(loop))
		return 0;

	if (!mh_race_poll_window_events(loop))
		return 0;

	if (!mh_race_poll_input(loop))
		return 0;

	if (!mh_race_sample_input_time(loop))
		return 0;

	if (!mh_race_update_chat(loop))
		return 0;

	if (!loop->tick_clock.rate && !mh_race_update_finish(loop))
		return 0;

	if (loop->capture == NULL && !loop->paused && !loop->screen.visible &&
		!(loop->session != NULL && loop->session->benchmark) &&
		(loop->continuous || loop->focused))
		loop->camera_preference = mh_field_camera_preference(
			loop->field, loop->camera_preference);

	if (!loop->tick_clock.rate && !mh_race_update_attract(loop))
		return 0;

	if (!loop->tick_clock.rate && !mh_race_read_driving_controls(loop))
		return 0;

	if (!mh_race_apply_view_controls(loop))
		return 0;

	if (!mh_race_update_pause_menu(loop))
		return 0;

	if (!mh_race_accept_results(loop))
		return 0;

	if (!mh_race_sample_simulation_time(loop))
		return 0;

	return 1;
}

static int
service_session(struct mh_race_loop *loop)
{
	int done;

	if (!loop->tick_clock.rate || loop->session == NULL ||
		loop->session->service == NULL)
		return 1;

	done = 0;

	if (loop->session->service(loop->session->context, &done,
		&loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Session service: %s\n", loop->error.message);
		loop->success = loop->running = 0;
		return 0;
	}

	if (done) {
		loop->running = 0;
		return 0;
	}

	return 1;
}

static int
advance_legacy_frame(struct mh_race_loop *loop)
{
	begin_frame_sound(loop);

	/*
	 * Consume the preceding physics publication before advancing physics.
	 */
	mh_race_feedback_frame(loop);

	if (!mh_race_remember_chase_camera(loop))
		return 0;

	if (!mh_race_advance_simulation(loop))
		return 0;

	if (!mh_race_update_tyre_effects(loop))
		return 0;

	if (!mh_race_update_chase_camera(loop))
		return 0;

	return 1;
}

/* Only animation and drawing may observe the interpolated field. */
static int
draw_presented_frame(struct mh_race_loop *loop)
{
	int drawn;

	drawn = present_field(loop) && mh_race_animate_scene(loop) &&
		mh_race_draw_frame(loop);
	restore_field(loop);

	return drawn;
}

static enum mh_app_status
step_race(struct mh_race_loop *loop)
{
	if (loop == NULL)
		return MH_APP_FAILED;

	loop->delay = 0;

	if (loop->loading_wait != NULL)
		return mh_race_startup_step(loop);

	if (!loop->running)
		return loop->success ? MH_APP_DONE : MH_APP_FAILED;

	if (!mh_race_pace_frame(loop))
		return loop->success ? MH_APP_RUNNING : MH_APP_FAILED;

	if (!prepare_frame(loop))
		goto frame_done;

	if (!service_session(loop))
		goto frame_done;

	if (loop->tick_clock.rate) {
		if (!run_fixed_ticks(loop))
			goto frame_done;
	} else if (!advance_legacy_frame(loop))
		goto frame_done;

	update_sound_positions(loop);

	mh_race_trace_camera(loop);

	if (!mh_race_update_results(loop))
		goto frame_done;

	if (!draw_presented_frame(loop))
		goto frame_done;

	if (!loop->tick_clock.rate)
		end_frame_sound(loop);

	if (!capture_frame(loop))
		goto frame_done;

	poll_screenshot(loop);

	mh_race_update_window_title(loop);

frame_done:
	restore_field(loop);
	if (!loop->running || !loop->success || loop->paused ||
		loop->menu.visible || loop->screen.visible || !loop->focused)
		mh_race_feedback_stop(loop);

	return !loop->success	? MH_APP_FAILED
		: loop->running ? MH_APP_RUNNING
				: MH_APP_DONE;
}

enum mh_app_status
mh_race_loop_step(struct mh_race_loop *loop)
{
	enum mh_app_status status;

	status = step_race(loop);

	if (status == MH_APP_FAILED && loop != NULL) {
		loop->running = 0;
		mh_race_loop_error(loop, &loop->error);
		mh_error_copy(&loop->error, &loop->error, loop->error_text,
			sizeof(loop->error_text));
	}

	return status;
}
