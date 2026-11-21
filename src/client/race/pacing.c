#include "client/race/internal.h"

/*
 * Gate the next rendered frame before sampling input or advancing fixed ticks.
 * Hosts sleep for the whole milliseconds returned in delay, then poll again
 * for the fractional remainder. No sleep or wait is hidden in a step call.
 */
int
mh_race_pace_frame(struct mh_race_loop *loop)
{
	long double now, remaining;
	unsigned long rate;

	rate = loop->context->settings == NULL
		? 0
		: loop->context->settings->max_fps;

	if (rate == 0 || loop->headless || loop->capture != NULL ||
		(loop->session != NULL && loop->session->benchmark)) {
		loop->pace_started = 0;
		return 1;
	}

	now = mh_clock_source_ms(loop->timer->source);

	if (now < 0) {
		mh_error_set(&loop->error, MH_ERROR_BACKEND,
			"Race presentation clock unavailable");
		fprintf(stderr, "Race presentation clock unavailable\n");
		loop->success = loop->running = 0;
		return 0;
	}

	/* Restart after a clock reset; never catch up missed render frames. */
	if (loop->pace_started && now >= loop->pace_previous) {
		remaining = loop->pace_deadline - now;

		if (remaining > 0) {
			loop->delay = (unsigned long)remaining;
			return 0;
		}
	}

	loop->pace_started = 1;
	loop->pace_previous = now;
	loop->pace_deadline = now + 1000.0L / rate;

	return 1;
}
