#include "engine/core/limits.h"
#include "engine/demo/benchmark.h"

enum mh_error_code
mh_demo_benchmark_step(struct mh_demo_benchmark *benchmark,
	const struct mh_demo_playback *playback, unsigned long clock_ms,
	struct mh_error *error_out)
{
	if (benchmark == NULL || playback == NULL || clock_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid benchmark sample");

	if (playback->active) {
		if (playback->anchor_ms != 0) {
			benchmark->frames =
				(benchmark->frames + 1) & MH_U32_MAX;
			benchmark->end_ms = clock_ms;
		} else {
			benchmark->complete = 0;
			benchmark->frames = 0;
			benchmark->start_ms = clock_ms;
		}
	} else
		benchmark->complete = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_benchmark_result(const struct mh_demo_benchmark *benchmark,
	double *seconds, double *fps, struct mh_error *error_out)
{
	long double duration;

	if (benchmark == NULL || seconds == NULL || fps == NULL ||
		!benchmark->complete || benchmark->frames == 0 ||
		benchmark->end_ms <= benchmark->start_ms)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"benchmark has no completed measurement");

	/* Subtract words as positive values, then seconds. */
	duration = ((long double)benchmark->end_ms - benchmark->start_ms) *
		(long double).001;
	*seconds = (double)duration;
	*fps = (double)((long double)benchmark->frames / duration);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
