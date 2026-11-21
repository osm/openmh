#ifndef MH_LEAGUE_UNLOCKS_H
#define MH_LEAGUE_UNLOCKS_H
#include "engine/core/error.h"
#include "engine/game/league.h"

#define MH_LEAGUE_UNLOCK_TRACKS 64
#define MH_LEAGUE_UNLOCK_REQUESTS 128

struct mh_league_unlock_plan {
	unsigned int	count, tracks[MH_LEAGUE_UNLOCK_REQUESTS];
};

/*
 * Select resources after the caller's League/live-session gate. Catalog order
 * is retained. Only a completed ordinary schedule emits requests: winners in
 * groups 3,2,1,0 each visit ordinary then special tracks in the active stage.
 * Before EACH request the caller checks current availability; successful writes
 * set it, failed writes leave it zero. Multiple human groups can repeat a
 * failed request. No I/O, RNG, league/availability mutation. Lists hold 16
 * ordinary and 16 special indices; oversized lists fail atomically.
 */
enum mh_error_code	mh_league_unlock_plan(const struct mh_league *league,
	const unsigned long *divisions, unsigned int count,
	struct mh_league_unlock_plan *output, struct mh_error *error_out);
#endif
