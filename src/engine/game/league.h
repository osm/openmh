#ifndef MH_LEAGUE_H
#define MH_LEAGUE_H

#include "engine/core/error.h"
#define MH_LEAGUE_ENTRIES 26
#define MH_LEAGUE_FINAL_SIZE 2
#define MH_LEAGUE_DIVISION_SIZE 8

enum mh_league_group {
	MH_LEAGUE_FINAL = 0,
	MH_LEAGUE_FIRST = 1,
	MH_LEAGUE_SECOND = 2,
	MH_LEAGUE_THIRD = 3,
	MH_LEAGUE_GROUPS = 4
};

/*
 * Outcome names describe a single human; mixed flags retain original
 * precedence.
 */
enum mh_league_outcome {
	MH_LEAGUE_ONGOING = 0,
	MH_LEAGUE_PROMOTED_SECOND = 1,
	MH_LEAGUE_PROMOTED_FIRST = 2,
	MH_LEAGUE_PROMOTED_FINAL = 3,
	MH_LEAGUE_CHAMPION = 4,
	MH_LEAGUE_STAY_IN_DIVISION = 5,
	MH_LEAGUE_RELEGATED = 6,
	MH_LEAGUE_FINAL_DEFEAT = 7
};

/*
 * Flat sorted groups: [0,2), [2,10), [10,18), [18,26).
 * Driver identifies caller-owned names, vehicles and other profile data;
 * that identity moves with the entry, never with a division slot.
 */
struct mh_league_entry {
	unsigned int	driver;
	unsigned long human, position, points, wins, second_places,
		third_places;
};

struct mh_league {
	struct mh_league_entry	entries[MH_LEAGUE_ENTRIES];
	unsigned long		race_index;
};

struct mh_league_schedule {
	unsigned int	stage, count, track_index, outcome;
	int		complete;
};

struct mh_standings;

/*
 * Season rollover only. Caller must establish schedule completion and process
 * track resources before invoking this once. No profile I/O. Resets
 * points/podiums, performs five complete-entry division swaps, resets race
 * index, and preserves last-race positions and identities. Rejects non-u32
 * fields atomically.
 */
enum mh_error_code	mh_league_rollover(struct mh_league *league,
	struct mh_error *error_out);

/*
 * Post-race points. All 26 positions must already be populated (1..8),
 * including other divisions' simulated results. A zero setting enables the
 * human adjustment in groups 1 and 2. Awards, stable-sorts each group and
 * advances race_index. Does not generate other-division results or perform
 * schedule rollover.
 */
enum mh_error_code	mh_league_award(struct mh_league *league,
	unsigned long setting, struct mh_error *error_out);

/*
 * Original inactive-division rank generation: draw rand()%count+1 and reject
 * previous ranks, consuming every rejected draw. Group 0 has two entries, other
 * groups eight. Caller chooses which divisions to simulate and supplies the
 * shared RNG state. Outputs unchanged on failure.
 */
enum mh_error_code	mh_league_simulate_group(struct mh_league *league,
	unsigned int group, unsigned long *random, struct mh_error *error_out);

/*
 * Race-setup rank prepass: simulate groups without any human, in group
 * order. Human groups retain their positions for the eventual race results.
 * Does not construct cars or select a track. Atomic on invalid input.
 */
enum mh_error_code	mh_league_prepare_positions(struct mh_league *league,
	unsigned long *random, struct mh_error *error_out);

/*
 * Installed Division codes in original installation order. track_index is an
 * index into that list when !complete; otherwise track_index==track_count.
 * outcome is zero until complete. No track loads or rollover performed. Empty
 * ordinary schedules are reported complete.
 */
enum mh_error_code	mh_league_schedule(const struct mh_league *league,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_league_schedule *output, struct mh_error *error_out);

/*
 * Copy active roster positions by stable driver identity, then award all
 * divisions. Caller invokes once for finalized race results; estimated rows are
 * accepted without changing physical-finish flags. Inactive rows leave their
 * prepared league positions unchanged. Missing/ambiguous identities or invalid
 * positions fail atomically.
 */
enum mh_error_code	mh_league_apply_results(struct mh_league *league,
	const struct mh_standings *standings, const unsigned int *drivers,
	unsigned int active_mask, unsigned long setting,
	struct mh_error *error_out);

/*
 * Completed-season outcome to zero-based DIVI archive index. Outcome zero means
 * no video and is rejected; output unchanged on error.
 */
enum mh_error_code	mh_league_video(unsigned int outcome,
	unsigned int *index, struct mh_error *error_out);

/*
 * Stable descending unsigned-u32 points order. Returns roster indices without
 * moving profiles.
 */
enum mh_error_code	mh_league_points_order(const unsigned long *points,
	unsigned int count, unsigned int *order, struct mh_error *error_out);

/*
 * HumanPlayer masks in sorted league groups: two-entry final, then three
 * eight-entry divisions. Outcome is evaluated at the end of the schedule
 * (caller must first establish schedule completion). Supports multiple-human
 * flag precedence; error is atomic.
 */
enum mh_error_code	mh_league_classify_groups(
	const unsigned int human_masks[MH_LEAGUE_GROUPS], unsigned int *stage,
	unsigned int *outcome, struct mh_error *error_out);

#endif
