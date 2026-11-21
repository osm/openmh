#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/application/application_internal.h"
#include "client/menus/internal.h"
#include "engine/core/error.h"
#include "engine/ui/menu_background_draw.h"
#include "engine/ui/menu_palette.h"
#include "engine/window/title.h"
#include "interfaces/clock/clock.h"
#include "interfaces/render/canvas.h"
#include "interfaces/render/capture.h"
#include "interfaces/window/window.h"

enum {
	MENU_FRAME_DELAY_MS = 16
};

static enum mh_error_code
capture_frame(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	status = mh_canvas_read(app->canvas, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_capture_image(
		app->capture, &image, app->capture_path, error_out);
	free(image.rgba);

	return status;
}

static void
poll_window_events(struct mh_application *app)
{
	struct mh_menus *menus;
	struct mh_event event;
	unsigned int scan;

	menus = app->menus;

	while (mh_input_event(&app->devices, &event)) {

		if (event.type == MH_EVENT_QUIT ||
			(event.type == MH_EVENT_WINDOW_CLOSE &&
				event.window == mh_window_id(app->window)))
			app->running = 0;

		if ((event.type == MH_EVENT_FOCUS_LOST ||
			event.type == MH_EVENT_FOCUS_GAINED ||
			event.type == MH_EVENT_INPUT_RESET) &&
			event.window == mh_window_id(app->window)) {
			memset(app->down, 0, sizeof(app->down));
			mh_keyboard_reset(&app->keyboard);

			if (app->idle_enabled)
				(void)mh_game_timer_real_word(menus->timer,
					&menus->idle_started, &menus->error);
		}

		if (event.type == MH_EVENT_KEY_DOWN ||
			event.type == MH_EVENT_KEY_UP) {
			if (event.window != mh_window_id(app->window))
				continue;

			if (app->capture_path == NULL &&
				!mh_input_focused(&app->devices))
				continue;

			scan = event.scan;

			if (scan != 0)
				app->down[scan] =
					event.type == MH_EVENT_KEY_DOWN ? 128
									: 0;
		}
	}
}

static enum mh_error_code
sample_input(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	struct mh_device_input device_input;
	unsigned long input_clock;
	enum mh_error_code status;

	menus = app->menus;
	status = MH_ERROR_NONE;

	if (app->capture_path != NULL) {
		menus->clock_ms =
			(app->frame_index * app->capture_step_ms) & MH_U32_MAX;
		menus->menu.elapsed_ms = app->capture_step_ms;
	}

	input_clock = menus->clock_ms;

	if (app->capture_path == NULL)
		status = mh_game_timer_real_word(
			menus->timer, &input_clock, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_input_poll(&app->devices, &app->keyboard, app->down,
			input_clock, menus->input_mode, &device_input,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Poll before drawing. Frontend consumers share the retained input
	 * clock and its elapsed word, including the first-poll 1/0 pair after
	 * reset. Captures supply explicit clocks.
	 */
	if (app->capture_path == NULL) {
		menus->clock_ms = app->keyboard.clock;
		menus->menu.elapsed_ms = app->keyboard.elapsed;
	}

	if (app->idle_enabled && app->keyboard.event_count != 0) {
		status = mh_game_timer_real_word(
			menus->timer, &menus->idle_started, error_out);
		menus->menu.idle_ms = mh_attract_idle_delay(
			menus->menu.current == &menus->main_page, 0, 0);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (app->capture_path == NULL &&
		app->session_options.debug.menu_clock) {
		printf("menu-clock %lu %lu %lu\n", input_clock, menus->clock_ms,
			menus->menu.elapsed_ms);
		fflush(stdout);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
advance_page(struct mh_application *app)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;

	mh_menus_set_input(menus, &app->keyboard, app->down);

	if (mh_menus_compact(menus)) {
		mh_menu_step(&menus->menu);
		return;
	}

	menus->digits.cursor = 0;
	status = mh_menu_backdrop_exchange(&menus->backdrop, MH_MENUS_WIDTH,
		MH_MENUS_HEIGHT, &menus->work, &menus->present, &menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_background_step(&menus->background,
			menus->menu.layout, menus->clock_ms,
			menus->menu.elapsed_ms, &menus->random_state,
			&menus->background_draw, &menus->error);

	if (status == MH_ERROR_NONE)
		status = mh_menu_background_render(&menus->background_draw,
			menus->background_models, MH_MENUS_WIDTH,
			MH_MENUS_HEIGHT, &menus->work, &menus->error);

	/* Submit before dispatching page draw/input. */
	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_submit(menus->palette,
			menus->settings->player_color, menus->present_palette,
			&menus->error);

	if (status == MH_ERROR_NONE)
		mh_menu_step(&menus->menu);
}

static void
poll_attract(struct mh_application *app)
{
	struct mh_menus *menus;
	long double idle_now, idle_stamp;
	int attract_requested;
	enum mh_error_code status;

	menus = app->menus;

	if (menus->error.code != MH_ERROR_NONE || !app->idle_enabled)
		return;

	idle_now = mh_game_timer_real_ms(menus->timer);
	idle_stamp = 0;

	if (idle_now - (long double)menus->idle_started >
		(long double)menus->menu.idle_ms)
		idle_stamp = mh_game_timer_real_ms(menus->timer);

	status = mh_attract_idle_poll(&menus->idle_started, menus->menu.idle_ms,
		idle_now, idle_stamp, &attract_requested, &menus->error);

	if (status == MH_ERROR_NONE && attract_requested) {
		menus->launch_mode = MH_GAME_ATTRACT;
		mh_menus_request(&menus->menu, &menus->attract_page);
	}
}

static void
apply_keyboard_reset(struct mh_application *app)
{
	struct mh_menus *menus;

	menus = app->menus;

	if (menus->keyboard_reset) {
		menus->keyboard_reset = 0;
		memset(app->down, 0, sizeof(app->down));
		mh_keyboard_reset(&app->keyboard);

		if (menus->error.code == MH_ERROR_NONE && app->idle_enabled)
			(void)mh_game_timer_real_word(menus->timer,
				&menus->idle_started, &menus->error);
	}
}

static void
apply_window_mode(struct mh_application *app)
{
	struct mh_error error;
	unsigned int fullscreen;

	if (app->capture_path != NULL)
		return;

	fullscreen = app->menus->settings->av.fullscreen;

	if (fullscreen == app->window_fullscreen)
		return;

	if (mh_window_set_fullscreen(app->window, fullscreen, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "Fullscreen: %s\n", error.message);
		app->menus->settings->av.fullscreen = app->window_fullscreen;
		return;
	}

	app->window_fullscreen = fullscreen;
	(void)mh_input_set_mouse_grab(&app->devices,
		fullscreen || app->menus->settings->av.grab_mouse,
		&app->menus->error);
}

static enum mh_error_code
present_frame(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_menus *menus;
	unsigned int i, pixel;
	enum mh_error_code status;

	menus = app->menus;

	for (i = 0; i < MH_MENUS_PIXELS; i++) {
		pixel = menus->present.pixels[i];
		memcpy(menus->rgba + i * 4, menus->present_palette + pixel * 3,
			3);
		menus->rgba[i * 4 + 3] = 255;
	}

	status = mh_canvas_draw(app->canvas, menus->rgba, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (app->capture_path != NULL &&
		app->frame_index + 1 == app->frame_limit) {
		status = capture_frame(app, error_out);
		app->success = status == MH_ERROR_NONE;

		if (status != MH_ERROR_NONE)
			return status;

		if (app->indexed_path != NULL) {
			status = mh_menus_save_indexed(app->indexed_path, menus,
				app->user_data, error_out);

			if (status != MH_ERROR_NONE) {
				app->success = 0;
				return status;
			}
		}
	}

	status = mh_canvas_present(app->canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (app->frame_index == 0)
		(void)mh_window_set_title(app->window,
			mh_window_title(app->window, "Motorhead",
				"Motorhead main-menu preview"),
			NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* DONE means the suspended menu frame can resume; it does not end the app. */
static enum mh_app_status
step_launched_session(struct mh_application *app)
{
	struct mh_menus *menus;
	enum mh_error_code status;

	menus = app->menus;

	if (app->resume_launch) {
		app->resume_launch = 0;
		app->launch_delay = 0;
	} else
		app->launch_status =
			mh_menus_launch_step(menus, &app->launch_delay);

	if (app->launch_delay != 0) {
		app->resume_launch = app->launch_status != MH_APP_RUNNING;
		app->delay = app->launch_delay;
		return MH_APP_RUNNING;
	}

	if (app->launch_status == MH_APP_RUNNING)
		return MH_APP_RUNNING;

	if (menus->error.code != MH_ERROR_NONE)
		return MH_APP_FAILED;

	status = mh_canvas_open(app->canvases, app->window, MH_MENUS_WIDTH,
		MH_MENUS_HEIGHT, &app->canvas, &menus->error);

	if (status != MH_ERROR_NONE)
		return MH_APP_FAILED;

	status = mh_input_open_from(app->inputs, &app->devices, app->window,
		app->window_fullscreen || menus->settings->av.grab_mouse,
		&menus->error);
	menus->input_mode = 1;

	if (status != MH_ERROR_NONE)
		return MH_APP_FAILED;

	memset(app->down, 0, sizeof(app->down));
	mh_keyboard_reset(&app->keyboard);

	if (app->idle_enabled)
		status = mh_game_timer_real_word(
			menus->timer, &menus->idle_started, &menus->error);

	if (status != MH_ERROR_NONE)
		return MH_APP_FAILED;

	/*
	 * Finish the suspended menu frame before sampling
	 * another.
	 */

	return MH_APP_DONE;
}

static enum mh_app_status
finish_frame(struct mh_application *app)
{
	struct mh_menus *menus;
	unsigned long elapsed;

	menus = app->menus;

	if (menus->exit_requested)
		app->running = 0;

	if (app->capture_path == NULL) {
		/*
		 * Native menu pacing keeps the original held-volume
		 * step from rounding to zero on fast machines. Captures
		 * use their supplied timestep and frame count.
		 */
		elapsed = (mh_clock_source_ticks(app->timer.source) -
				app->frame_started) &
			MH_U32_MAX;

		if (elapsed < MENU_FRAME_DELAY_MS)
			app->delay = MENU_FRAME_DELAY_MS - elapsed;
	}

	app->frame_index++;

	return MH_APP_RUNNING;
}

enum mh_app_status
mh_application_step_menus(struct mh_application *app)
{
	enum mh_app_status status;
	struct mh_menus *menus;

	menus = app->menus;

	if (!app->running ||
		(app->frame_limit != 0 && app->frame_index >= app->frame_limit))
		goto finished;

	if (menus->launch != NULL || app->resume_launch) {
		status = step_launched_session(app);

		if (status == MH_APP_RUNNING)
			return status;

		if (status == MH_APP_FAILED)
			goto finished;

		goto frame_finished;
	}

	app->frame_started = mh_clock_source_ticks(app->timer.source);

	poll_window_events(app);

	if (!app->running || menus->error.code != MH_ERROR_NONE)
		goto finished;

	if (sample_input(app, &menus->error) != MH_ERROR_NONE)
		goto finished;

	advance_page(app);

	/* Initial page entry clears pending transitions, so queue after it. */
	if (menus->error.code == MH_ERROR_NONE &&
		app->connect_address != NULL) {
		if (mh_menus_multiplayer_connect(menus, app->connect_address,
			MH_NET_LANINET_PORT,
			&menus->error) != MH_ERROR_NONE)
			goto finished;

		app->connect_address = NULL;
	}

	apply_window_mode(app);

	poll_attract(app);

	apply_keyboard_reset(app);

	if (menus->error.code == MH_ERROR_NONE &&
		!mh_menu_audio_update(menus->audio))
		mh_error_set(&menus->error, MH_ERROR_BACKEND,
			"cannot update frontend audio");

	if (menus->error.code != MH_ERROR_NONE)
		goto finished;

	if (present_frame(app, &menus->error) != MH_ERROR_NONE)
		return mh_application_stop(app, 1);

	if (app->capture_path == NULL && menus->launch_requested) {
		struct mh_settings *settings;

		settings = app->preferences == NULL ? &app->defaults
						: app->preferences;
		mh_input_close(&app->devices);
		mh_canvas_destroy(app->canvas);
		app->canvas = NULL;
		app->session_options.screenshot_encoder =
			app->screenshot_encoders[settings->av
					.screenshot_format];
		mh_menus_launch_open(menus, app->window, app->asset_root,
			app->asset_root, settings, app->settings_store,
			&app->session_options, app->audio_mode);

		if (menus->launch != NULL)
			return MH_APP_RUNNING;

		if (menus->error.code != MH_ERROR_NONE)
			goto finished;
	}

frame_finished:
	return finish_frame(app);

finished:
	if (menus->error.code == MH_ERROR_NONE && app->capture_path == NULL)
		app->success = 1;

	return mh_application_stop(app, !app->success);
}
