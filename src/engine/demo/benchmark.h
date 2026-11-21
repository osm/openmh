#ifndef MH_DEMO_BENCHMARK_H
#define MH_DEMO_BENCHMARK_H

#include "engine/demo/playback.h"

/* Count before playback advances, using the ordinary game clock. */
struct mh_demo_benchmark {
	unsigned long	frames, start_ms, end_ms;
	int		complete;
};

enum mh_error_code	mh_demo_benchmark_step(
	struct mh_demo_benchmark *benchmark,
	const struct mh_demo_playback *playback, unsigned long clock_ms,
	struct mh_error *error_out);

/* Only completed runs with a positive measured interval have an FPS result. */
enum mh_error_code	mh_demo_benchmark_result(
	const struct mh_demo_benchmark *benchmark, double *seconds, double *fps,
	struct mh_error *error_out);

#endif
