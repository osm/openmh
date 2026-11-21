#include "client/race/console.h"
#include "client/race/internal.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"

static enum mh_error_code
update_finish(struct mh_race_loop *loop, struct mh_error *error_out)
{
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	visuals = &loop->context->scene.visuals;
	status = loop->field->attract
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_race_scores_frame(loop->context, loop->field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_camera_finish(loop->field,
			!loop->continuous && loop->drive->race.finished &&
				!loop->screen.finish_seen &&
				loop->screen.finish_anchor_ms == 0,
			error_out);

	if (status == MH_ERROR_NONE && !loop->continuous)
		status = loop->capture == NULL
			? mh_results_screen_frame(&loop->screen, loop->field,
				loop->tick_clock.rate ? 0 : loop->elapsed,
				error_out)
			: mh_results_screen_update_local(&loop->screen,
				&loop->field->standings,
				(loop->field->active_mask & 1) != 0,
				mh_drive_clock_ms(loop->drive), error_out);

	if (status == MH_ERROR_NONE && !loop->field->attract &&
		!loop->continuous && loop->screen.finish_seen &&
		!loop->screen.prepared)
		status = mh_results_screen_prepare(&loop->screen, loop->field,
			loop->tick_clock.rate
				? &loop->field->ai_random
				: &visuals->particle_system.random,
			error_out);

	if (status == MH_ERROR_NONE && loop->capture != NULL)
		status = mh_field_camera_update(loop->field, 0, error_out);

	return status;
}

int
mh_race_update_finish(struct mh_race_loop *loop)
{
	if (loop->session != NULL && loop->session->benchmark)
		return 1;

	if (!loop->continuous &&
		(loop->paused || !loop->focused || loop->screen.visible))
		return 1;

	if (update_finish(loop, &loop->error) != MH_ERROR_NONE) {
		fprintf(stderr, "Finish presentation: %s\n",
			loop->error.message);
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	return 1;
}

int
mh_race_accept_results(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (loop->screen.prepared &&
		!mh_audio_results(loop->audio, loop->screen.aborted)) {
		mh_audio_close(loop->audio);
		loop->audio = NULL;
	}

	/*
	 * European 3.1 also bypasses the board for aborted Single races.
	 * Time Attack, Ghost and Demo prepare results on either exit.
	 * A native storage failure keeps a retryable board instead of
	 * losing the race. OS window close was handled above without
	 * acceptance.
	 */
	if (loop->result_handler != NULL && loop->screen.prepared &&
		(loop->accept_results ||
			(loop->session != NULL && loop->session->external_ui) ||
			(!mh_results_board_required(
				loop->menu_input.mode, loop->screen.aborted) &&
				!loop->save_failed))) {
		/*
		 * CD playback is independent of original save/UI work.
		 * Start demand filling before a potentially slow fsync;
		 * failed saves keep this same stream and cursor while
		 * the board is retried.
		 */
		if ((loop->menu_input.mode == MH_GAME_QUICK_RACE ||
			loop->menu_input.mode == MH_GAME_LEAGUE) &&
			!mh_audio_continue_results(loop->audio)) {
			fprintf(stderr,
				"Results music continuation unavailable\n");
			mh_audio_close(loop->audio);
			loop->audio = NULL;
		}

		status = loop->result_handler->commit(
			loop->result_handler->context, &loop->screen.final,
			&loop->error);

		if (status == MH_ERROR_NONE) {
			*loop->completed = 1;
			loop->running = 0;
			return 0;
		}

		if (loop->session != NULL && loop->session->external_ui) {
			loop->success = loop->running = 0;
			return 0;
		}

		fprintf(stderr, "Save results: %s\n", loop->error.message);
		loop->save_failed = 1;

		/*
		 * The original repeat timer can fire during a slow
		 * save. Require release before another attempt, just as
		 * when opening the board.
		 */
		loop->results_ready = 0;
	}

	return 1;
}

static enum mh_error_code
close_pause_menu(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	loop->menu.page = MH_PAUSE_MAIN;
	loop->menu_input.keys = MH_PAUSE_TOGGLE;
	status = mh_pause_update(&loop->menu, &loop->menu_settings,
		&loop->menu_input, &loop->menu_output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_pause(&loop->context->scene, 0,
			loop->context->settings, error_out);

	return status;
}

int
mh_race_update_results(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (loop->continuous)
		mh_results_screen_update(
			&loop->screen, &loop->field->standings);

	/*
	 * A network finisher can enter results while navigating a menu.
	 * Release its sound records and dimming before showing results.
	 */
	if (loop->screen.visible && loop->menu.visible) {
		status = close_pause_menu(loop, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Closing pause menu: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		/* Local preparation already stopped effects before the save. */
		if (!loop->screen.prepared &&
			!mh_audio_menu(loop->audio, &loop->menu_output, 0)) {
			mh_audio_close(loop->audio);
			loop->audio = NULL;
		}
	}

	if (loop->capture == NULL &&
		((loop->drive->race.finished && loop->results_shown == 0) ||
			(loop->field->standings.complete &&
				loop->results_shown < 2))) {
		mh_race_print_results(stdout, loop->field);
		loop->results_shown = loop->field->standings.complete ? 2 : 1;
	}

	return 1;
}
