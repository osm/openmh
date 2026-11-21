#ifndef MH_MENU_UNLOCKS_H
#define MH_MENU_UNLOCKS_H

#include "engine/core/error.h"
#define MH_MENU_TRACKS 64
#define MH_MENU_DIVISION_GROUPS 4
#define MH_MENU_DIVISION_TRACKS 16

struct mh_menu_track_unlocks {
	unsigned int	selected, visible, unlocked;
	unsigned long record_class; /* Retained record class, set to 1 if
				restricted. */
	unsigned int total[MH_MENU_DIVISION_GROUPS],
		ordinary[MH_MENU_DIVISION_GROUPS];
	unsigned long completed[MH_MENU_DIVISION_GROUPS]; /* Original u32
				counters, including decrement wrap. */
	unsigned int	indices[MH_MENU_DIVISION_GROUPS][MH_MENU_DIVISION_TRACKS];

	unsigned long divisions[MH_MENU_TRACKS]; /* Stock values below 100
						become 100. */
};

struct mh_menu_car_unlocks {
	unsigned int	selected, visible, unlocked;
};

/*
 * Post-load track availability. Patched rules omit the old final-marker
 * decrement after full completion. Caller supplies the already sorted catalog
 * and decoded availability words. Four groups, <=16 tracks each, Division <500.
 * Unused result indices are zero. record_class preserves its opaque value
 * unless the restricted-list branch sets it to one. Reject empty
 * prefixes/out-of-range access; all failures preserve output.
 */
/*
 * Named add-ons form a leading prefix with Division >=
 * MH_TRACK_CUSTOM_DIVISION. They are always selectable and do not contribute to
 * stock progression.
 */
enum mh_error_code	mh_menu_track_unlocks(const unsigned long *divisions,
	const unsigned long *available, unsigned int count,
	unsigned int selected, unsigned long record_class,
	struct mh_menu_track_unlocks *output, struct mh_error *error_out);

/*
 * Post-load car availability, using normalized track divisions and the track
 * producer's prefix counts. The car catalog must already be sorted; keys are
 * u32, counts 1..4096. Special-car disc/page restrictions follow later.
 */
enum mh_error_code	mh_menu_car_unlocks(const unsigned long *cars,
	unsigned int car_count, unsigned int selected,
	const unsigned long *tracks, const unsigned long *available,
	unsigned int track_count, unsigned int visible_tracks,
	unsigned int unlocked_tracks, struct mh_menu_car_unlocks *output,
	struct mh_error *error_out);

#endif
