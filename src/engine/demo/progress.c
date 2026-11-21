#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/progress.h"

#define CROSSING_WORDS 4
#define SPLIT_NOTICE_MAX 3

enum mh_error_code
mh_demo_progress_correct(const struct mh_demo_player *recording,
	const struct mh_demo_progress *progress,
	struct mh_demo_progress *output, struct mh_error *error_out)
{
	struct mh_demo_progress next;
	unsigned int row, column, lap;

	if (recording == NULL || progress == NULL || output == NULL ||
		progress->lap < 1 ||
		progress->lap >= MH_DEMO_CROSSING_ROWS - 1 ||
		progress->split_notice > SPLIT_NOTICE_MAX ||
		(progress->finished != 0 && progress->finished != 1) ||
		progress->now_ms > MH_U32_MAX ||
		progress->best_ms > MH_U32_MAX ||
		progress->lap_ms > MH_U32_MAX ||
		progress->last_ms > MH_U32_MAX ||
		progress->split_ms > MH_U32_MAX ||
		progress->score > MH_U32_MAX ||
		progress->best_recorded_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded progress");

	for (row = 0; row < MH_DEMO_CROSSING_ROWS; row++)
		for (column = 0; column < CROSSING_WORDS; column++)
			if (recording->crossings[row][column] > MH_U32_MAX ||
				progress->crossings[row][column] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"recorded crossing exceeds disk word");

	next = *progress;
	lap = next.lap;

	for (row = 0; row < MH_DEMO_CROSSING_ROWS; row++)
		for (column = 0; column < CROSSING_WORDS; column++)
			if (next.crossings[row][column] != 0)
				next.crossings[row][column] =
					recording->crossings[row][column];

	if (next.finished) {
		next.now_ms = recording->crossings[lap + 1][0];
		next.last_ms = (next.now_ms - recording->crossings[lap][0]) &
			MH_U32_MAX;
		next.lap_ms = next.last_ms;
		next.score =
			((unsigned long)MH_I32_MAX - next.now_ms) & MH_U32_MAX;
	} else {
		if (next.split_notice > 1)
			next.split_ms =
				(recording->crossings[lap][next.split_notice] -
					recording->crossings[lap][0]) &
				MH_U32_MAX;

		if (lap <= 1) {
			next.last_ms = 0;
			next.best_ms = 0;
			*output = next;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		next.last_ms = (recording->crossings[lap][0] -
				recording->crossings[lap - 1][0]) &
			MH_U32_MAX;
	}

	if (next.last_ms < next.best_recorded_ms)
		next.best_recorded_ms = next.last_ms;

	next.best_ms = next.best_recorded_ms;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
