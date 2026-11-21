#include "client/loading/view.h"
#include "client/race/internal.h"

#define LOADING_POLL_MS 16

static int
needs_confirmation(const struct mh_race_loop *loop, int requested)
{
	return requested && loop->loading != NULL &&
		!loop->debug.skip_loading_wait && loop->capture == NULL &&
		!loop->headless && !loop->field->network &&
		!loop->field->attract;
}

enum mh_error_code
mh_race_startup_open(struct mh_race_loop *loop, int wait_for_loading,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (!needs_confirmation(loop, wait_for_loading)) {
		if (!mh_race_initialize(loop)) {
			loop->success = 0;
			return mh_race_loop_error(loop, error_out);
		}

		loop->initialized = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_loading_view_ready(loop->loading, loop->inputs, error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"cannot prepare loading confirmation");

	mh_loading_view_retain(loop->loading);
	loop->loading_wait = loop->loading;
	loop->running = loop->success = 1;

	if (loop->debug.window_titles)
		fprintf(stderr, "Loading ready: press any key to continue\n");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_app_status
mh_race_startup_step(struct mh_race_loop *loop)
{
	enum mh_loading_action action;
	enum mh_error_code status;

	status =
		mh_loading_view_wait(loop->loading_wait, &action, &loop->error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Loading screen: %s\n", loop->error.message);
		loop->success = 0;
	} else if (action == MH_LOADING_WAIT) {
		loop->delay = LOADING_POLL_MS;
		return MH_APP_RUNNING;
	} else if (action == MH_LOADING_QUIT) {
		if (loop->quit != NULL)
			*loop->quit = 1;
	} else {
		/*
		 * Initialize clocks and controls only after the confirming key
		 * is up.
		 */
		loop->initialized = mh_race_initialize(loop);

		if (!loop->initialized)
			loop->success = 0;
	}

	if (!loop->success)
		mh_error_copy(&loop->error, &loop->error, loop->error_text,
			sizeof(loop->error_text));

	mh_loading_view_close(loop->loading_wait);
	loop->loading_wait = loop->loading = NULL;

	if (!loop->initialized)
		loop->running = 0;

	return !loop->success	? MH_APP_FAILED
		: loop->running ? MH_APP_RUNNING
				: MH_APP_DONE;
}
