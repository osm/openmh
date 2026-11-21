#include "client/race/internal.h"
#include "engine/core/error.h"

#define CONTROL_FALLBACK_SECONDS .00001f
#define VIEW_READY_MS 6001.0

void
mh_race_reset_tick_clock(struct mh_race_loop *loop)
{
	if (loop->tick_clock.rate == 0)
		return;

	mh_tick_clock_init(&loop->tick_clock, loop->tick_clock.rate, NULL);
	mh_tick_input_init(&loop->tick_input);
	loop->tick_restarted = 1;
	loop->presentation_history = 0;
	loop->wall_elapsed = 0;
}

enum mh_error_code
mh_race_restart_clock(const struct mh_clock_source *clock,
	struct mh_field *field, struct mh_clock_counter *previous,
	struct mh_error *error_out)
{
	struct mh_clock_counter before, after, frequency;
	struct mh_clock_counter now;
	long double elapsed;
	enum mh_error_code status;

	now = mh_clock_source_counter(clock);
	before = *previous;
	after = now;
	frequency = mh_clock_source_frequency(clock);
	status = mh_clock_counter_elapsed(
		&before, &after, &frequency, 1, &elapsed, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_restart_clock(field,
			field->cars[0].frame.fixed_rate ? 0 : elapsed,
			error_out);

	if (status == MH_ERROR_NONE)
		*previous = now;

	return status;
}

int
mh_race_sample_frame_clock(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	loop->entry_menu = loop->menu.visible;
	loop->now = mh_clock_source_counter(loop->timer->source);
	loop->previous_counter = loop->previous;
	loop->current_counter = loop->now;
	loop->frequency_counter =
		mh_clock_source_frequency(loop->timer->source);
	status = mh_clock_counter_elapsed(&loop->previous_counter,
		&loop->current_counter, &loop->frequency_counter, 1,
		&loop->wall_elapsed, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Frame counter: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	mh_race_frame_rate_sample(loop);
	loop->previous = loop->now;
	loop->chat_block =
		loop->chat_enabled && loop->session->chat->active != 0;
	loop->dismissed_results = 0;
	loop->brightness_changed = 0;
	loop->accept_results = 0;
	loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];

	return 1;
}

static enum mh_error_code
sample_frame_request(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_frame_menu_policy(loop->menu_input.mode, loop->entry_menu,
		loop->menu.visible, loop->wall_elapsed, &loop->frame_request,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!loop->continuous &&
		(loop->paused || !loop->focused ||
			mh_results_screen_pauses(
				&loop->screen, &loop->field->standings))) {
		loop->frame_request.elapsed = 0;
		loop->frame_request.action = MH_FRAME_SUSPEND;
	}

	return status;
}

int
mh_race_sample_input_time(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	status = sample_frame_request(loop, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Frame menu policy: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	loop->elapsed = loop->frame_request.elapsed;
	loop->control_seconds = (float)(loop->elapsed > 1 ? 1 : loop->elapsed);

	if (loop->control_seconds <= 0)
		loop->control_seconds = CONTROL_FALLBACK_SECONDS;

	loop->control_racing = mh_drive_racing(loop->drive);
	loop->view_clock = loop->drive->countdown
		? mh_drive_intro_ms(loop->drive)
		: VIEW_READY_MS;

	if (loop->capture == NULL) {
		/*
		 * Preview without mutation: a rejected physics frame
		 * must retain its clock, while input sees the same GO
		 * transition as physics.
		 */
		status = mh_drive_frame_sample(loop->drive,
			loop->tick_clock.rate ? 0 : loop->elapsed,
			&loop->input_frame, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Input clock: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		loop->control_seconds = loop->input_frame.time.elapsed;
		loop->control_racing = loop->input_frame.time.go_ms != 0;

		if (loop->drive->countdown)
			loop->view_clock =
				(double)((long double)loop->input_frame
							.seconds *
						1000.0L -
					loop->input_frame.intro_ms);
	}

	return 1;
}

int
mh_race_sample_simulation_time(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	status = sample_frame_request(loop, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Frame menu policy: %s\n", loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	loop->elapsed = loop->frame_request.elapsed;
	loop->frame_updated = loop->capture == NULL &&
		loop->frame_request.action != MH_FRAME_SUSPEND;
	loop->steps = 0;

	return 1;
}
