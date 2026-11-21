#include <stdio.h>
#include <string.h>

#include "client/race/internal.h"

#define SAMPLE_SECONDS .5L

enum {
	DISPLAY_LIMIT = 9999,
	TEXT_BYTES = 16,
	TEXT_MARGIN = 4
};

static int
enabled(const struct mh_race_loop *loop)
{
	return loop->context->settings != NULL &&
		loop->context->settings->show_fps && !loop->headless &&
		loop->capture == NULL;
}

void
mh_race_frame_rate_sample(struct mh_race_loop *loop)
{
	long double rate;

	if (!enabled(loop))
		return;

	/*
	 * Wall time includes rendering and pacing, even while gameplay is
	 * paused.
	 */
	loop->fps_frames++;
	loop->fps_elapsed += loop->wall_elapsed;

	if (loop->fps_elapsed < SAMPLE_SECONDS)
		return;

	rate = (long double)loop->fps_frames / loop->fps_elapsed;
	loop->fps = rate >= DISPLAY_LIMIT ? DISPLAY_LIMIT
					: (unsigned int)(rate + .5L);
	loop->fps_frames = 0;
	loop->fps_elapsed = 0;
}

int
mh_race_frame_rate_draw(struct mh_race_loop *loop)
{
	char text[TEXT_BYTES];
	int success;

	if (!enabled(loop))
		return 1;

	if (loop->fps == 0)
		strcpy(text, "-- FPS");
	else
		sprintf(text, "%u FPS", loop->fps);
	success = MH_RENDER_BUILD_SOFTWARE &&
			loop->context->scene.assets.profile ==
				MH_RENDER_SOFTWARE
		? mh_software_hud_label(&loop->context->software_hud, text,
			TEXT_MARGIN, &loop->ui, &loop->error) == MH_ERROR_NONE
		: mh_hud_label(
			&loop->context->hud, text, TEXT_MARGIN, &loop->ui);

	if (!success) {
		fprintf(stderr, "Cannot draw frame rate\n");
		loop->success = loop->running = 0;
	}

	return success;
}
