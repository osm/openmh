#ifndef MH_MENU_RACE_SETUP_H
#define MH_MENU_RACE_SETUP_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"

enum mh_menu_race_choice {
	MH_MENU_RACE_CHOICE_TRACK = 0,
	MH_MENU_RACE_CHOICE_LAPS,
	MH_MENU_RACE_CHOICE_CATCH_UP
};

struct mh_menu_race_setup_state {
	unsigned int selected, track,
		catch_up; /* Row 0..2, catalog index, Off/On. */
	unsigned long	laps;

	unsigned long
		track_auxiliary; /* Cleared by input; purpose unresolved. */
};

/*
 * Select the previous or next single-player preset for a 32-bit lap count.
 */
enum mh_error_code	mh_menu_race_laps(unsigned long laps, int forward,
	unsigned long *output, struct mh_error *error_out);

/*
 * Left/Right change the current row's value, then Up/Down move rows, then
 * Enter/Escape request pages. Only event value 1 fires. Mode 2 (Time attack)
 * limits Down to row 1; it does not clamp a retained row 2. visible_tracks
 * includes locked entries; unlocked_tracks is a nonempty prefix. Both counts
 * are bounded to 4096, preserving caller catalog order. Escape from a locked
 * track selects unlocked_tracks-1 before returning. State and ordered events
 * commit together; invalid requests change neither. Init/draw and departure
 * palette restoration belong to the compositor.
 */
enum mh_error_code	mh_menu_race_setup_update(
	struct mh_menu_race_setup_state *state, unsigned int mode,
	unsigned int visible_tracks, unsigned int unlocked_tracks,
	const struct mh_menu_keys *input, struct mh_menu_output *output,
	struct mh_error *error_out);

#endif
