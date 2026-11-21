#include <string.h>

#include "client/race/internal.h"
#include "engine/core/error.h"

static void
update_camera_shortcut(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (!(loop->session != NULL && loop->session->benchmark) &&
		loop->context->controls == NULL && !loop->screen.visible &&
		(!loop->paused || loop->continuous)) {
		if (loop->event.shortcut == MH_SHORTCUT_F1)
			mh_field_camera_mode(
				loop->field, MH_CAMERA_IN_CAR, NULL);

		if (loop->event.shortcut == MH_SHORTCUT_F2)
			mh_field_camera_mode(
				loop->field, MH_CAMERA_CHASE, NULL);

		if (loop->event.shortcut == MH_SHORTCUT_F3)
			mh_field_camera_mode(
				loop->field, MH_CAMERA_TRACK, NULL);

		if (loop->event.shortcut == MH_SHORTCUT_F4) {
			status = mh_field_cycle_view(loop->field, &loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Camera selection: %s\n",
					loop->error.message);
				loop->success = loop->running = 0;
			}

			loop->viewed =
				&loop->field
					->cars[mh_field_viewed(loop->field)];
		}
	}
}

int
mh_race_poll_window_events(struct mh_race_loop *loop)
{
	unsigned int scan;

	while (mh_input_event(&loop->devices, &loop->event)) {

		if (loop->event.type == MH_EVENT_QUIT ||
			(loop->event.type == MH_EVENT_WINDOW_CLOSE &&
				loop->window != NULL &&
				loop->event.window ==
					mh_window_id(loop->window))) {
			loop->running = 0;

			if (loop->quit != NULL)
				*loop->quit = 1;

			if (loop->result_handler != NULL)
				break;
		}

		if (loop->event.type == MH_EVENT_FOCUS_LOST &&
			loop->window != NULL &&
			loop->event.window == mh_window_id(loop->window)) {
			/*
			 * External frontends own pause; focus still gates
			 * input/time.
			 */
			if (!loop->continuous &&
				(loop->session == NULL ||
					!loop->session->external_ui))
				loop->paused = 1;

			loop->focused = 0;
			memset(loop->down, 0, sizeof(loop->down));
		}

		if (loop->event.type == MH_EVENT_FOCUS_GAINED &&
			loop->window != NULL &&
			loop->event.window == mh_window_id(loop->window)) {
			loop->focused = 1;

			/*
			 * Attract playback resumes automatically on focus.
			 */
			if (loop->field->attract)
				loop->paused = 0;
		}

		if (loop->event.type == MH_EVENT_INPUT_RESET &&
			loop->window != NULL &&
			loop->event.window == mh_window_id(loop->window)) {
			memset(loop->down, 0, sizeof(loop->down));
			mh_keyboard_reset(&loop->keyboard);
		}

		if (loop->event.type != MH_EVENT_KEY_DOWN &&
			loop->event.type != MH_EVENT_KEY_UP)
			continue;

		if (loop->window != NULL &&
			loop->event.window != mh_window_id(loop->window))
			continue;

		if (!loop->focused || loop->capture != NULL)
			continue;

		if (loop->event.repeat)
			continue;

		scan = loop->event.scan;

		if (scan != 0)
			loop->down[scan] = loop->event.type == MH_EVENT_KEY_DOWN
				? MH_KEY_DOWN_BIT
				: 0;

		if (loop->event.type != MH_EVENT_KEY_DOWN)
			continue;

		/*
		 * Attract mode uses the original bound controls and raw
		 * exit keys.
		 */
		if (loop->field->attract)
			continue;

		/* Raw typing must not change the camera. */
		if (loop->chat_block)
			continue;

		if (loop->result_handler != NULL && loop->screen.prepared) {
			if (loop->results_ready &&
				(loop->event.shortcut == MH_SHORTCUT_ENTER ||
					loop->event.shortcut ==
						MH_SHORTCUT_ESCAPE))
				loop->accept_results = 1;

			/*
			 * Preserve the finalized board and RNG across
			 * save retries.
			 */
			continue;
		}

		if (!loop->menu.visible)
			update_camera_shortcut(loop);
	}

	return loop->running != 0;
}
