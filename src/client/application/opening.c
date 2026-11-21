#include <stdio.h>
#include <stdlib.h>

#include "client/application/opening.h"
#include "interfaces/video/video.h"
#include "interfaces/window/window.h"

struct opening {
	const struct mh_video_provider	*videos;
	const struct mh_resource_source	*resources;
	const struct mh_window_system	*windows;
	struct mh_window		*window;
	char				reference[1024];
	unsigned int			index, count;
	int	mute, fullscreen, window_acquired, finished, *outcome;
	struct mh_video_player		*player;
	int				video_outcome;
};

static void
close_opening(void *context)
{
	struct opening *opening;

	opening = (struct opening *)context;
	mh_video_close(opening->player);

	if (opening->window_acquired)
		mh_window_system_release(opening->windows);

	free(opening);
}

static enum mh_app_status
step_opening(void *context, unsigned long *delay, struct mh_error *failure)
{
	struct opening *opening;
	struct mh_video_options options;
	struct mh_error error;
	enum mh_video_status status;
	int outcome;

	(void)failure;
	opening = (struct opening *)context;

	if (opening->finished)
		return MH_APP_DONE;

	outcome = MH_OPENING_UNAVAILABLE;

	if (opening->player == NULL) {
		printf("Opening movie: %u\n", opening->index);
		fflush(stdout);
		options.resources = opening->resources;
		options.window = opening->window;
		options.reference = opening->reference;
		options.collection = 1;
		options.index = opening->index;
		options.mute = opening->mute;
		options.fullscreen = opening->fullscreen;
		options.capture = NULL;
		options.capture_frame = 0;

		if (mh_video_open_from(opening->videos, &options,
			&opening->video_outcome, &opening->player,
			&error) != MH_ERROR_NONE)
			fprintf(stderr, "Movie playback: %s\n", error.message);
	}

	if (opening->player != NULL) {
		status = mh_video_step(opening->player);
		*delay = mh_video_delay(opening->player);

		if (status == MH_VIDEO_RUNNING)
			return MH_APP_RUNNING;

		if (mh_video_close(opening->player))
			outcome = opening->video_outcome == MH_VIDEO_CLOSED
				? MH_OPENING_CLOSED
				: opening->video_outcome;

		opening->player = NULL;
	}

	outcome = mh_opening_video_result(
		opening->index, opening->count, outcome);

	if (outcome == MH_OPENING_CONTINUE) {
		opening->index++;
		return MH_APP_RUNNING;
	}

	*opening->outcome = outcome;
	opening->finished = 1;

	return MH_APP_DONE;
}

static const struct mh_activity_ops opening_ops = {step_opening, close_opening};

enum mh_error_code
mh_opening_begin(const struct mh_video_provider *videos,
	const struct mh_resource_source *resources,
	const struct mh_window_system *windows, struct mh_window *window,
	const char *disc, int mute, int fullscreen,
	const struct mh_content_intro *intro, int *outcome,
	struct mh_activity *output, struct mh_error *error_out)
{
	struct opening *opening;
	struct mh_error error;
	enum mh_error_code status;
	int found;

	if (resources == NULL || windows == NULL || disc == NULL ||
		intro == NULL ||
		(intro->collection == NULL) != (intro->count == 0) ||
		(mute != 0 && mute != 1) ||
		(fullscreen != 0 && fullscreen != 1) || outcome == NULL ||
		output == NULL || mh_activity_is_open(output))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid opening sequence");

	*outcome = MH_OPENING_FAILED;
	opening = (struct opening *)calloc(1, sizeof(*opening));

	if (opening == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening movie sequence");

	opening->videos = videos;
	opening->resources = resources;
	opening->windows = windows;
	opening->window = window;
	opening->mute = mute;
	opening->fullscreen = fullscreen;
	opening->outcome = outcome;
	opening->count = intro->count;

	if (intro->count == 0) {
		opening->finished = 1;
		*outcome = MH_OPENING_ENDED;
		goto ready;
	}

	if (videos == NULL) {
		fprintf(stderr,
			"Opening movies: unavailable on this platform; continuing to menu\n");
		opening->finished = 1;
		*outcome = MH_OPENING_ENDED;
		goto ready;
	}

	status = mh_video_resolve_from(videos, resources, disc,
		intro->collection, opening->reference,
		sizeof(opening->reference), &found, &error);

	if (status != MH_ERROR_NONE || !found) {
		fprintf(stderr, "Opening movies: %s; continuing to menu\n",
			status == MH_ERROR_NONE ? "archive not found"
						: error.message);
		opening->finished = 1;
		*outcome = MH_OPENING_ENDED;
	} else if (window == NULL) {
		/*
		 * Keep the window system acquired across the player-owned
		 * windows.
		 */
		if (mh_window_system_acquire(windows, NULL, &error) !=
			MH_ERROR_NONE) {
			fprintf(stderr, "Opening movies: %s\n", error.message);
			free(opening);
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot initialize opening movie video");
		}

		opening->window_acquired = 1;
	}

ready:
	status = mh_activity_start(output, opening, &opening_ops, error_out);

	if (status != MH_ERROR_NONE)
		close_opening(opening);

	return status;
}
