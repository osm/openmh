#include <emscripten.h>

#include "backends/runtime/emscripten_provider.h"

enum {
	BACKGROUND_FRAME_MS = 100
};

/*
 * emscripten_loop.js owns the visibility listener for the main loop's lifetime.
 */
void	mh_browser_loop_watch_visibility(int enabled, int interval_ms);

void
mh_browser_loop_start(void (*step)(void))
{
	emscripten_set_main_loop(step, 0, 0);
	mh_browser_loop_watch_visibility(1, BACKGROUND_FRAME_MS);
}

void
mh_browser_loop_cancel(void)
{
	mh_browser_loop_watch_visibility(0, 0);
	emscripten_cancel_main_loop();
}

double
mh_browser_loop_time(void)
{
	return emscripten_get_now();
}
