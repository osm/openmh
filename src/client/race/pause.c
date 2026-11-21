#include <string.h>

#include "client/race/internal.h"
#include "engine/core/error.h"

static unsigned int
menu_keys(const struct mh_keyboard *keyboard, unsigned int toggle)
{
	unsigned int keys;

	keys = keyboard->events[MH_KEY_ESCAPE] ? MH_PAUSE_ESCAPE : 0;

	if (toggle)
		keys |= MH_PAUSE_TOGGLE;

	if (keyboard->events[MH_KEY_ENTER])
		keys |= MH_PAUSE_ENTER;

	if (keyboard->events[MH_KEY_UP])
		keys |= MH_PAUSE_UP;

	if (keyboard->events[MH_KEY_DOWN])
		keys |= MH_PAUSE_DOWN;

	if (keyboard->events[MH_KEY_LEFT])
		keys |= MH_PAUSE_LEFT;

	if (keyboard->events[MH_KEY_RIGHT])
		keys |= MH_PAUSE_RIGHT;

	return keys;
}

static enum mh_error_code
prepare_quit(struct mh_race_loop *loop, struct mh_error *error_out)
{
	unsigned long *random;

	random = loop->tick_clock.rate
		? &loop->field->ai_random
		: &loop->context->scene.visuals.particle_system.random;

	/* Quit does not abort an actual finisher. */
	if (!loop->drive->race.finished)
		return mh_results_screen_abort(
			&loop->screen, loop->field, random, error_out);

	loop->screen.finish_seen = loop->screen.visible = 1;

	return mh_results_screen_prepare(
		&loop->screen, loop->field, random, error_out);
}

enum mh_error_code
mh_race_loop_leave(struct mh_race_loop *loop, struct mh_error *error_out)
{
	if (loop == NULL || loop->session == NULL ||
		!loop->session->external_ui)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"race is not externally controlled");

	return prepare_quit(loop, error_out);
}

static enum mh_error_code
restart_race(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_race_records_save(loop->context, loop->field, error_out);

	if (status == MH_ERROR_NONE && !loop->continuous &&
		!loop->drive->frame.pending) {
		status = mh_field_clock_stage(loop->field,
			loop->tick_clock.rate ? 0 : loop->frame_request.elapsed,
			error_out);

		if (status == MH_ERROR_NONE)
			loop->wall_elapsed = 0;
	}

	if (status == MH_ERROR_NONE) {
		if (!loop->live)
			status = mh_field_reset(loop->field, 1, error_out);
		else if (loop->session->restart == NULL)
			status = mh_error_set(error_out, MH_ERROR_STATE,
				"session restart is unavailable");
		else
			status = loop->session->restart(
				loop->session->context, error_out);
	}

	if (status == MH_ERROR_NONE) {
		if (!loop->continuous)
			mh_race_records_restarted(loop->context);

		mh_race_reset_tick_clock(loop);

		memset(&loop->shaped_controls, 0,
			sizeof(loop->shaped_controls));
		loop->results_shown = loop->results_ready = 0;
		memset(&loop->screen, 0, sizeof(loop->screen));
		mh_race_restore_camera(loop);
		mh_race_scene_skids_reset(&loop->context->scene);
		mh_audio_reset(loop->audio);
		mh_race_feedback_stop(loop);
		mh_keyboard_reset(&loop->keyboard);

		if (!loop->continuous)
			status = mh_race_restart_clock(loop->timer->source,
				loop->field, &loop->previous, error_out);
	}

	return status;
}

static enum mh_error_code
apply_menu_actions(struct mh_race_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i, entry;

	status = MH_ERROR_NONE;

	/*
	 * Flat shading and perspective subdivision configure
	 * the original software rasterizer; the hardware path
	 * only changes reflections.
	 */
	for (i = 0; i < loop->menu_output.count; i++) {
		if (loop->menu_output.events[i].kind == MH_PAUSE_INPUT_MODE)
			loop->input_mode = loop->menu_output.events[i].value;

		if (loop->menu_output.events[i].kind == MH_PAUSE_REFLECTION)
			loop->reflections =
				loop->menu_output.events[i].value != 0;

		if (loop->menu_output.events[i].kind == MH_PAUSE_DISTANCE) {
			for (entry = 0; entry < loop->field->count &&
				status == MH_ERROR_NONE;
				entry++)
				status = mh_drive_view_distance(
					&loop->field->cars[entry],
					loop->menu_settings.distance,
					error_out);
		}

		if (loop->menu_output.events[i].kind == MH_PAUSE_RESTART)
			status = restart_race(loop, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
leave_race(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (!loop->continuous) {
		if (loop->field->attract) {
			if (loop->attract_exit != NULL)
				*loop->attract_exit =
					loop->menu_output.demo_exit;

			status = mh_field_clock_advance(
				loop->field, loop->elapsed, &loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Attract exit clock: %s\n",
					loop->error.message);
				loop->success = 0;
			}

			loop->running = 0;

			return 0;
		}

		if (loop->result_handler == NULL) {
			loop->running = 0;
			return 0;
		}

		status = prepare_quit(loop, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Race exit results: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		loop->results_ready = 0;
		mh_keyboard_reset(&loop->keyboard);
	} else {
		status = loop->session->restart == NULL
			? mh_error_set(&loop->error, MH_ERROR_STATE,
				"lobby return is unavailable")
			: loop->session->restart(
				loop->session->context, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Return to lobby: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		/*
		 * The session's next advance records
		 * host/client return.
		 */
	}

	return 1;
}

int
mh_race_update_pause_menu(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	if (loop->session != NULL && loop->session->external_ui)
		return 1;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	if (!loop->screen.visible && loop->capture == NULL) {
		loop->was_menu = loop->menu.visible;
		loop->menu_input.keys = loop->focused &&
				!loop->dismissed_results && !loop->chat_block
			? menu_keys(&loop->keyboard,
				loop->context->controls != NULL
					? loop->actions[MH_CONTROL_GAME_MENU]
					: loop->keyboard
						.events[MH_KEY_ESCAPE])
			: 0;

		loop->menu_input.cd_playing =
			mh_audio_music_playing(loop->audio);
		status = mh_pause_update_glenz(&loop->menu,
			&loop->menu_settings, &loop->menu_input,
			loop->glenz_loaded, &loop->menu_output, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Pause menu: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		if (!mh_audio_menu(loop->audio, &loop->menu_output,
			loop->menu.visible)) {
			mh_audio_close(loop->audio);
			loop->audio = NULL;
		}

		status = apply_menu_actions(loop, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Pause action: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		if (!loop->menu_output.keep_running && !leave_race(loop))
			return 0;

		loop->brightness_changed =
			visuals->pause_dim != loop->menu_output.dimmed;
		status = mh_race_scene_pause(scene, loop->menu_output.dimmed,
			loop->context->settings, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Pause lighting: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		if (loop->was_menu != loop->menu.visible)
			loop->paused = 0;
	}

	return 1;
}
