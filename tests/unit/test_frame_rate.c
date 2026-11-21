#include <stdio.h>
#include <string.h>

#include "client/race/internal.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "frame rate:%d: %s\n", __LINE__, #x);  \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_loop loop;
static struct mh_race_context context;

int
main(void)
{
	struct mh_av_settings settings;
	unsigned int i;

	memset(&settings, 0, sizeof(settings));
	loop.context = &context;
	context.settings = &settings;
	settings.show_fps = 1;
	loop.wall_elapsed = .01L;

	for (i = 0; i < 100; i++)
		mh_race_frame_rate_sample(&loop);

	CHECK(loop.fps == 100);

	/*
	 * Gameplay pause and simulation delta do not change the measured rate.
	 */
	loop.paused = 1;
	loop.elapsed = 0;
	loop.wall_elapsed = .02L;

	for (i = 0; i < 100; i++)
		mh_race_frame_rate_sample(&loop);

	CHECK(loop.fps == 50);

	/* Alternating frame costs must be averaged by elapsed time. */
	for (i = 0; i < 200; i++) {
		loop.wall_elapsed = i % 2 ? .04L : .01L;
		mh_race_frame_rate_sample(&loop);
	}

	CHECK(loop.fps >= 39 && loop.fps <= 41);
	settings.show_fps = 0;
	loop.fps = 123;
	loop.wall_elapsed = 1;
	mh_race_frame_rate_sample(&loop);
	CHECK(loop.fps == 123);
	CHECK(mh_race_frame_rate_draw(&loop) && loop.ui.count == 0);
	settings.show_fps = 1;
	loop.capture = "deterministic capture";
	mh_race_frame_rate_sample(&loop);
	CHECK(loop.fps == 123 && mh_race_frame_rate_draw(&loop));
	loop.capture = NULL;
	loop.headless = 1;
	mh_race_frame_rate_sample(&loop);
	CHECK(loop.fps == 123 && mh_race_frame_rate_draw(&loop));
	puts("Frame rate: elapsed-time averaging, paused play and capture isolation passed");

	return 0;
}
