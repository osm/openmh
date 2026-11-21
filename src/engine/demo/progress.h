#ifndef MH_DEMO_PROGRESS_H
#define MH_DEMO_PROGRESS_H

#include "engine/assets/demo.h"
#include "engine/core/error.h"

#define MH_DEMO_INITIAL_BEST_MS 666666666UL

/*
 * The caller detects checkpoint crossings and advances the lap. Replay
 * corrects the resulting times using the recording's unsigned 32-bit clock.
 */
struct mh_demo_progress {
	unsigned int	lap, split_notice;
	int		finished;
	unsigned long	now_ms, best_ms, lap_ms, last_ms, split_ms, score;

	/* Per-live-slot cache. */
	unsigned long	best_recorded_ms;
	unsigned long	crossings[MH_DEMO_CROSSING_ROWS][4];
};

/*
 * Call only for active replayed entrants while playback is active. Ghost
 * callers resolve the source player through their recording-slot map.
 * Initialize the cached best to MH_DEMO_INITIAL_BEST_MS once at load/restart,
 * not on each lap. Only nonzero live crossings receive the saved word (which
 * may be zero). All differences and finished score use original modular u32
 * arithmetic. No geometry, race-clock advancement or lap detection. Atomic on
 * failure.
 */
enum mh_error_code	mh_demo_progress_correct(
	const struct mh_demo_player *recording,
	const struct mh_demo_progress *progress,
	struct mh_demo_progress *output, struct mh_error *error_out);

#endif
