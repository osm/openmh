#ifndef MH_STANDINGS_ORDER_H
#define MH_STANDINGS_ORDER_H

#include "engine/core/error.h"
#include "engine/game/race_limits.h"

struct mh_standing_key {
	unsigned long	score, total_ms;
	int		finished;
};

struct mh_standing_order {
	unsigned int	entries[MH_RACE_MAX_CARS];
	unsigned int	count, finished_count;
};

/*
 * Samples arrive in roster order. Partition finishers by swaps, then sort
 * finish times ascending and unfinished progress descending as binary32.
 * Equal keys retain partition order. Accepts zero entries and unsigned
 * 32-bit keys; callers own membership and narrower clock limits.
 * Failure preserves output.
 */
enum mh_error_code	mh_standings_order(const struct mh_standing_key *keys,
	unsigned int count, struct mh_standing_order *output,
	struct mh_error *error_out);

#endif
