#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/progress.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_demo_player recording;
	struct mh_demo_progress progress, corrected, saved_progress;

	memset(&recording, 0, sizeof(recording));
	memset(&progress, 0, sizeof(progress));
	progress.lap = 1;
	progress.best_recorded_ms = MH_DEMO_INITIAL_BEST_MS;
	recording.crossings[1][2] = 4000;
	recording.crossings[2][0] = 10000;
	recording.crossings[2][2] = 14500;
	recording.crossings[3][0] = 21000;
	progress.crossings[1][2] = 4025;
	progress.crossings[1][3] = 5000;
	progress.split_notice = 2;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) == MH_ERROR_NONE);
	CHECK(corrected.crossings[1][2] == 4000 &&
		corrected.crossings[1][3] == 0 &&
		corrected.crossings[2][0] == 0);
	CHECK(corrected.split_ms == 4000 && corrected.last_ms == 0 &&
		corrected.best_ms == 0);
	CHECK(corrected.best_recorded_ms == MH_DEMO_INITIAL_BEST_MS);
	progress = corrected;
	progress.lap = 2;
	progress.crossings[2][0] = 10025;
	progress.lap_ms = 30;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) == MH_ERROR_NONE);
	CHECK(corrected.last_ms == 10000 && corrected.best_ms == 10000 &&
		corrected.best_recorded_ms == 10000);
	CHECK(corrected.lap_ms == 30 && corrected.split_ms == 4500);
	progress = corrected;
	progress.finished = 1;
	progress.now_ms = 21008;
	progress.split_ms = 12;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) == MH_ERROR_NONE);
	CHECK(corrected.now_ms == 21000 && corrected.last_ms == 11000 &&
		corrected.lap_ms == 11000 && corrected.split_ms == 12);
	CHECK(corrected.best_ms == 10000 &&
		corrected.score == (unsigned long)MH_I32_MAX - 21000);
	progress = corrected;
	progress.finished = 0;
	progress.lap = 1;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) == MH_ERROR_NONE);
	CHECK(corrected.last_ms == 0 && corrected.best_ms == 0 &&
		corrected.best_recorded_ms == 10000);
	progress.lap = 2;
	progress.finished = 1;
	recording.crossings[3][0] = 0;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) == MH_ERROR_NONE);
	CHECK(corrected.last_ms == 4294957296UL && corrected.best_ms == 10000);
	memcpy(&saved_progress, &corrected, sizeof(corrected));
	progress.lap = 0;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
	progress.lap = 26;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
	progress.lap = 25;
	progress.split_notice = 4;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
	progress.split_notice = 0;
	progress.finished = 2;
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
	progress.finished = 0;
	CHECK(mh_demo_progress_correct(NULL, &progress, &corrected, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_progress_correct(&recording, NULL, &corrected, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_demo_progress_correct(&recording, &progress, NULL, NULL) !=
		MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	progress.best_recorded_ms = (MH_U32_MAX + 1UL);
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
	progress.best_recorded_ms = 0;
	recording.crossings[26][3] = (MH_U32_MAX + 1UL);
	CHECK(mh_demo_progress_correct(&recording, &progress, &corrected,
		NULL) != MH_ERROR_NONE);
#endif
	CHECK(memcmp(&corrected, &saved_progress, sizeof(corrected)) == 0);
	puts("recorded progress: accepted crossings, display times, cache lifetime, wrap and atomic bounds passed");

	return 0;
}
