#ifndef MH_UI_RACE_GAPS_H
#define MH_UI_RACE_GAPS_H

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/race.h"

#define MH_GAPS_MAX_CARS MH_RACE_MAX_CARS

enum mh_gap_mode {
	MH_GAPS_OFF = 0,
	MH_GAPS_NEIGHBORS = 1,
	MH_GAPS_ALL = 2
};

struct mh_race_gap {
	/* Position is the checkpoint order; zero hides it. */
	unsigned int	entry, position;

	/* Other car minus viewed car at the same crossing. */
	long		milliseconds;
};

struct mh_race_gaps {
	unsigned int		mode, count;
	struct mh_race_gap	rows[MH_GAPS_MAX_CARS];
};

/*
 * Race gaps. Mode 0 off, 1 leader and neighbors (excluding self), 2 all
 * recorded crossings (including self). Finish forces mode 2. Split-hold
 * duration zero hides every mode. Stable roster indices; no allocation, clock
 * changes or standings mutation. Times are bounded to signed-32 milliseconds;
 * output is atomic on failure.
 */
enum mh_error_code	mh_race_gaps(const struct mh_race *const *cars,
	unsigned int count, unsigned int active_mask, unsigned int viewed,
	unsigned int mode, struct mh_race_gaps *output,
	struct mh_error *error_out);

#endif
