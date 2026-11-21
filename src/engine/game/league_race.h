#ifndef MH_LEAGUE_RACE_H
#define MH_LEAGUE_RACE_H
#include "engine/core/error.h"
#include "engine/game/league.h"
#include "engine/game/standings.h"

struct mh_league_roster {
	unsigned int	stage, count;
	unsigned int	drivers[MH_LEAGUE_DIVISION_SIZE];
	unsigned int	slots[MH_LEAGUE_DIVISION_SIZE];
};

enum mh_league_race_state {
	MH_LEAGUE_RACE_UNINITIALIZED = 0,
	MH_LEAGUE_RACE_PREPARED = 1,
	MH_LEAGUE_RACE_AWARDED = 2
};

/*
 * Native single-division race transaction, not an original disk structure.
 * Keep this object alive while displaying/reopening the same results.
 */
struct mh_league_race {
	struct mh_league	before, prepared, awarded;
	unsigned int		drivers[MH_LEAGUE_DIVISION_SIZE], count;

	/* One of mh_league_race_state; retained as int in the native layout. */
	int			state;
};

/*
 * Original single-human construction: local human at entry zero, then AI
 * in current division order. AI car selection uses profile.cars[stage];
 * the human uses the current menu car, not that saved profile field.
 * Eight-car grid reverses division order; final human uses slot 1, AI slot 0.
 * Rejects zero/multiple humans and duplicate IDs.
 * Output unchanged on failure.
 */
enum mh_error_code	mh_league_roster(const struct mh_league *league,
	struct mh_league_roster *output, struct mh_error *error_out);

/*
 * Bind an entire human-containing division in caller-selected race order.
 * Other divisions must have no humans. Reject completed schedules and
 * ambiguous identities. Simulate inactive divisions once, consuming the
 * supplied shared RNG; do not modify the persistent league. Atomic errors.
 * Start a fresh transaction for a new race, not for results-screen updates.
 */
enum mh_error_code	mh_league_race_begin(const struct mh_league *league,
	const unsigned long *divisions, unsigned int track_count,
	const unsigned int *drivers, unsigned int count, unsigned long *random,
	struct mh_league_race *output, struct mh_error *error_out);

/*
 * Caller supplies finalized standings (including original estimates).
 * All roster rows must be present. Publish one award atomically; subsequent
 * calls for this transaction are no-ops. Reject a changed destination league,
 * including replacement by a different season with the same race index.
 * Saving/retrying disk writes is separate and must not re-award the race.
 */
enum mh_error_code	mh_league_race_finish(struct mh_league_race *race,
	const struct mh_standings *standings, unsigned long setting,
	struct mh_league *league, struct mh_error *error_out);

#endif
