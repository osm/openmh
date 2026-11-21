#ifndef MH_MENU_RANKINGS_H
#define MH_MENU_RANKINGS_H

#include "engine/core/error.h"
#include "engine/game/highscore.h"
#include "engine/ui/menu.h"

#define MH_MENU_RANKINGS_TRACK_LIMIT 4096
#define MH_MENU_RANKINGS_CATEGORIES 3
#define MH_MENU_RANKINGS_DIVISIONS 3
#define MH_MENU_RANKINGS_DIFFICULTIES 3

enum mh_menu_rankings_column {
	MH_MENU_RANKINGS_CATEGORY = 0,
	MH_MENU_RANKINGS_TRACK =
		1, /* Division for the dormant League category. */
	MH_MENU_RANKINGS_LAPS = 2,
	MH_MENU_RANKINGS_DIFFICULTY = 3,
	MH_MENU_RANKINGS_RECORD_CLASS = 4
};

struct mh_menu_rankings_state {
	unsigned int focus, category, track, division, lap_choice, difficulty,
		record_class;
};

struct mh_menu_rankings_input {
	unsigned int	escape, up, down, left, right;
};

struct mh_menu_rankings_output {
	struct mh_menu_output	events;

	unsigned int
		reload; /* Once, after every event, if any value changed. */
};

/*
 * Embedded/BSS state before the first page entry. The original retains
 * focus and division across entries; initialization below does not clear them.
 */
void	mh_menu_rankings_defaults(struct mh_menu_rankings_state *state);

/*
 * Modes 0/1 select Single Race; others Best Race. Mode 1 retains the previous
 * track/difficulty; other modes copy the current selections. Laps map to the
 * first choice >= laps, clamped to the final choice. Record class is always
 * copied. Caller resets animation and loads the table.
 */
enum mh_error_code	mh_menu_rankings_init(
	struct mh_menu_rankings_state *state, unsigned int mode,
	unsigned int track, unsigned long laps, unsigned int difficulty,
	unsigned int record_class, unsigned int track_count,
	struct mh_error *error_out);

/*
 * Escape, Up, Down, Left, Right, then reload. Only event value 1 fires.
 * Category 3 and focus 4 have original branches but cannot be selected by
 * ordinary forward navigation. No Enter action. Atomic errors.
 */
enum mh_error_code	mh_menu_rankings_update(
	struct mh_menu_rankings_state *state, unsigned int track_count,
	const struct mh_menu_rankings_input *input,
	struct mh_menu_rankings_output *output, struct mh_error *error_out);

/*
 * Resolve the selected category and lap-choice table to a query. TrackName is
 * borrowed from names; league does not need a track name. All filenames are
 * checked with the high-score core's boundary policy.
 */
enum mh_error_code	mh_menu_rankings_query(
	const struct mh_menu_rankings_state *state, const char *const *names,
	unsigned int track_count, struct mh_highscore_query *output,
	struct mh_error *error_out);

#endif
