#ifndef MH_STANDINGS_H
#define MH_STANDINGS_H

#include "engine/core/error.h"
#include "engine/game/race_limits.h"

#define MH_STANDINGS_MAX_CARS MH_RACE_MAX_CARS

/* Samples are in stable roster order, not last tick's ranking order. */
struct mh_standing_sample {
	unsigned long	score, total_ms, best_ms;
	unsigned int	best_lap;
	int		finished;
};

struct mh_race_result {
	unsigned int	position, best_lap;

	/* Estimated result time does not imply physical finish. */
	int		finished, estimated;
	unsigned long	score, finish_ms, best_ms;

	/* Finished time minus winner time; zero while unfinished. */
	long		gap_ms;
};

struct mh_standings {
	/* Roster indexed. */
	struct mh_race_result	cars[MH_STANDINGS_MAX_CARS];

	/* Entry indices, leader first. */
	unsigned int		order[MH_STANDINGS_MAX_CARS];
	unsigned int		count, finished_count;
	int			complete;
};

/*
 * Partition finishers by swaps, stable ascending binary32 times, then stable
 * descending binary32 progress scores. Result fields expose actual finishes
 * only, with no estimated times for unfinished racers. C89, no
 * allocation/SDL/globals. All output unchanged on failure.
 */
enum mh_error_code	mh_standings_rank(const struct mh_standing_sample *cars,
	unsigned int count, struct mh_standings *output,
	struct mh_error *error_out);

/*
 * Classify participating entries only; retain departed rows after them with
 * position zero and their recorded times. complete/finished_count refer to
 * active entries. The roster indices remain stable for assets and identity.
 */
enum mh_error_code	mh_standings_rank_active(
	const struct mh_standing_sample *cars, unsigned int count,
	unsigned int active_mask, struct mh_standings *output,
	struct mh_error *error_out);

#endif
