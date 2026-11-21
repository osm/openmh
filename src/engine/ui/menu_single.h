#ifndef MH_MENU_SINGLE_H
#define MH_MENU_SINGLE_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_choices.h"
#include "interfaces/ui/layout.h"

#define MH_MENU_SINGLE_NAME_BYTES 128
#define MH_MENU_SINGLE_LABELS 12

struct mh_menu_single_state {
	/* Choice 0..4; CFG mode 0..3; catalog index. */
	unsigned int	selected, mode, car;
};

struct mh_menu_single_item {
	const char	*name;
	unsigned long	division;
};

struct mh_menu_single_details {
	const struct mh_menu_single_item	*cars, *tracks;
	unsigned int				car_count, track_count;
	unsigned int	stage, race_index, track, laps, difficulty, automatic;
	const char				*demo_name;
};

struct mh_menu_single_label {
	long		x, y;
	unsigned int	color;
	char		text[MH_MENU_SINGLE_NAME_BYTES];
};

struct mh_menu_single_summary {
	struct mh_menu_single_label	labels[MH_MENU_SINGLE_LABELS];
	unsigned int			count;
};

/*
 * Downgrade Ghostmode to Time attack when the demo list is empty, then process
 * Left, Right, Enter, Escape in order. Only key event 1 fires. PAGE requests
 * retain order; their later leave callback commits the mode.
 */
enum mh_error_code	mh_menu_single_update(
	struct mh_menu_single_state *state, unsigned long demo_count,
	const struct mh_menu_input *input, struct mh_menu_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out);

/*
 * Every departure, including Escape, commits selected-1 when the selection is
 * nonzero. Quick race (zero) preserves the current mode.
 */
enum mh_error_code	mh_menu_single_leave(struct mh_menu_single_state *state,
	struct mh_error *error_out);

/*
 * Original information panel, using layout 10. League draws can clamp
 * state.car; preserve the original count-then-prefix eligibility rule even for
 * unsorted catalogs. stage is the league human stage 0..3; race_index selects
 * the filtered ordinary league track list. Names are bounded byte-font strings
 * (127 bytes max). Cars/tracks are in original installation order, 1..4096
 * entries. The caller owns their memory. No rendering, asset loading, RNG or OS
 * types. Errors preserve state/output.
 */
enum mh_error_code	mh_menu_single_summary(
	const struct mh_menu_layout *layout, unsigned int mix,
	struct mh_menu_single_state *state,
	const struct mh_menu_single_details *details,
	struct mh_menu_single_summary *output, struct mh_error *error_out);

#endif
