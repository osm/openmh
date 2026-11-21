#ifndef MH_HIGHSCORE_AWARDS_H
#define MH_HIGHSCORE_AWARDS_H

#include "engine/core/error.h"
#include "engine/game/highscore.h"

#define MH_HIGHSCORE_AWARD_LIMIT 3

/*
 * Zero once at process/session startup. Fresh race setup clears completed
 * alone; a grid restart retains both words.
 */
struct mh_highscore_history {
	unsigned long	completed, race_ms;
};

struct mh_highscore_result {
	/* Saved completion and record-class state. */
	unsigned long			completed, mode;
	struct mh_highscore_query	query; /* kind/division are ignored. */

	/* auxiliary is always written as zero. */
	struct mh_highscore_candidate	candidate;
};

/*
 * One exit/restart boundary, owning its strings and any pending write.
 * Keep this object through retries; begin again only for a new boundary.
 * No pointers into itself: copying the object retains a complete snapshot.
 */
struct mh_highscore_awards {
	int				initialized, pending;
	unsigned int count, next_kind, kinds[MH_HIGHSCORE_AWARD_LIMIT],
		ranks[MH_HIGHSCORE_AWARD_LIMIT];
	unsigned int			laps, difficulty, record_class;
	unsigned long			transmission, lap_ms, race_ms;
	char track[MH_HIGHSCORE_TEXT_LIMIT], name[MH_HIGHSCORE_NAME_SIZE],
		car[MH_HIGHSCORE_CAR_SIZE];
	struct mh_highscore_table	table;
};

struct mh_highscore_store;

/*
 * Any nonzero local finish publishes completed=1 and total. An unfinished
 * observation retains both words. Invalid input is atomic.
 */
enum mh_error_code	mh_highscore_history_frame(
	struct mh_highscore_history *history, unsigned long finished,
	unsigned long race_ms, struct mh_error *error_out);

/*
 * Completed==1 awards Single (mode==0), then Best Race. Nonzero best lap
 * independently awards Best Lap. No eligibility/recovery filter, no league
 * aggregate award, no zero-time race sentinel. Callers skip this routine for
 * modes 5/6. Begin is atomic; it owns a bounded snapshot and performs no
 * storage operations.
 */
enum mh_error_code	mh_highscore_awards_begin(
	const struct mh_highscore_result *result,
	struct mh_highscore_awards *awards, struct mh_error *error_out);

/*
 * Load, qualify, insert, save in original order. Stop at the first error;
 * earlier commits remain committed. Retrying resumes the pending category.
 * Once finished, repeated calls do no I/O. This retry policy is native;
 * the original Windows writer does not provide such an error interface.
 */
enum mh_error_code	mh_highscore_awards_save(
	struct mh_highscore_awards *awards,
	const struct mh_highscore_store *store, struct mh_error *error_out);

#endif
