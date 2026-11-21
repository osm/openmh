#include <stdlib.h>

#include "client/loading/view.h"
#include "client/race/internal.h"
#include "engine/core/error.h"

/*
 * Retained across race sessions. Intro history survives Glide teardown/setup;
 * only the mesh timer restarts. The saved time member is not restored.
 */
static struct mh_race_history history;
static struct mh_race_loop *active_race;
static char open_error_text[MH_RACE_ERROR_SIZE];

int
mh_race_club_credits(void)
{
	return history.hud.club_credits || history.software_hud.club_credits;
}

enum mh_error_code
mh_race_loop_open(struct mh_race_context *context, struct mh_field *field,
	const struct mh_race_loop_options *options,
	struct mh_race_loop **output, struct mh_error *error_out)
{
	struct mh_race_loop *loop;
	enum mh_error_code status;

	if (context == NULL || field == NULL || options == NULL ||
		output == NULL || options->timer == NULL ||
		options->windows == NULL || options->inputs == NULL ||
		options->renderers == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race loop arguments");

	if (active_race != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"a race loop is already open");

	loop = (struct mh_race_loop *)calloc(1, sizeof(*loop));

	if (loop == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening race loop");

	mh_screenshot_capture_init(&loop->screenshot, NULL, options->root,
		options->user_data, options->screenshot_encoder,
		options->screenshot_directory);
	loop->debug = options->debug;
	loop->renderers = options->renderers;
	loop->loading = options->loading;
	loop->windows = options->windows;
	loop->window = options->window;
	loop->inputs = options->inputs;
	loop->audio_provider = options->audio_provider;
	loop->retro_source = options->retro_source;
	loop->audio_decoder = options->audio_decoder;
	loop->audio_trace = options->audio_trace;
	loop->timer = options->timer;
	loop->history = &history;
	loop->context = context;
	loop->field = field;
	loop->authoritative_field = field;
	loop->capture = options->capture;
	loop->headless = options->headless;
	loop->results = options->results;
	loop->root = options->root;
	loop->audio_options = options->audio_options;
	loop->trace = options->trace;
	loop->session = options->session;
	loop->result_handler = options->result_handler;
	loop->completed = options->completed;
	loop->quit = options->quit;
	loop->music = options->music;
	loop->attract_exit = options->attract_exit;

	if (field->cars[0].frame.fixed_rate != 0)
		mh_tick_clock_init(&loop->tick_clock,
			field->cars[0].frame.fixed_rate, NULL);

	mh_tick_input_init(&loop->tick_input);

	status = mh_race_startup_open(
		loop, options->wait_for_loading, error_out);

	if (status != MH_ERROR_NONE) {
		if (error_out != NULL)
			mh_error_copy(error_out, error_out, open_error_text,
				sizeof(open_error_text));

		free(loop);

		return status;
	}

	active_race = loop;
	*output = loop;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_loop_error(const struct mh_race_loop *loop, struct mh_error *error_out)
{
	if (loop == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing race loop");

	if (loop->success)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (loop->error.code == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_FAILED, "race failed");

	return mh_error_forward(error_out, &loop->error);
}

unsigned long
mh_race_loop_delay(const struct mh_race_loop *loop)
{
	return loop == NULL ? 0 : loop->delay;
}

static void
save_view_settings(const struct mh_race_loop *loop)
{
	struct mh_av_settings *settings;

	settings = loop->context->settings;

	if (settings == NULL)
		return;

	settings->car_view = loop->camera_preference == MH_CAMERA_IN_CAR
		? MH_CAR_VIEW_INSIDE
		: MH_CAR_VIEW_OUTSIDE;
	settings->car_shading = loop->menu_settings.shading;
	settings->perspective = loop->menu_settings.perspective;
	settings->view_distance = loop->menu_settings.distance;
	settings->effects_volume = loop->menu_settings.effects_volume;
	settings->music_volume = loop->menu_settings.cd_volume;
	settings->road_map = loop->map_mode;
	settings->checkpoint_info = loop->gap_mode;
	settings->name_plates = loop->names_mode;
}

static void
transfer_results_music(struct mh_race_loop *loop)
{
	if (loop->music != NULL && loop->success && loop->completed != NULL &&
		*loop->completed && (loop->quit == NULL || !*loop->quit)) {
		if (mh_audio_continue_results(loop->audio)) {
			*loop->music = loop->audio;
			loop->audio = NULL;
		} else
			fprintf(stderr,
				"Results music continuation unavailable\n");
	}
}

int
mh_race_loop_close(struct mh_race_loop *loop)
{
	int success;

	if (loop == NULL)
		return 0;

	if (!loop->initialized) {
		mh_loading_view_close(loop->loading_wait);
		success = loop->success;
		active_race = NULL;
		free(loop);
		return success;
	}

	if (loop->process_clock && loop->field->cars[0].frame.active) {
		loop->current_counter = loop->previous;
		mh_game_timer_leave(
			loop->timer, loop->field, &loop->current_counter);
	}

	save_view_settings(loop);
	transfer_results_music(loop);

	mh_screenshot_capture_close(&loop->screenshot);
	mh_input_close(&loop->devices);
	mh_audio_close(loop->audio);
	mh_renderer_destroy(loop->renderer);

	if (loop->owns_window)
		mh_window_destroy(loop->window);

	mh_window_system_release(loop->windows);

	success = loop->success;
	active_race = NULL;
	free(loop);

	return success;
}
