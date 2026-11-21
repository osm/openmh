#ifndef MH_MENU_CAR_SETUP_H
#define MH_MENU_CAR_SETUP_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"

enum mh_menu_car_choice {
	MH_MENU_CAR_CHOICE_CAR = 0,
	MH_MENU_CAR_CHOICE_TRANSMISSION,
	MH_MENU_CAR_CHOICE_HORN,
	MH_MENU_CAR_CHOICE_RECORD
};

struct mh_menu_car_setup_state {
	unsigned int	selected, car, horn, automatic, record;
};

struct mh_menu_car_catalog {
	const unsigned long
		*divisions; /* Original order; do not filter or sort. */
	unsigned int	count, visible, unlocked, saved_visible;
};

#define MH_MENU_CAR_EVENTS 9

/* Four directional sounds, up to three Escape events, then two Enter events. */
struct mh_menu_car_output {
	struct mh_menu_event	events[MH_MENU_CAR_EVENTS];
	unsigned int		count;
};

/*
 * Adjust the catalog. If special_allowed is zero, or mode is League (1),
 * visible becomes the count of Division%100<10 entries. Treat that many leading
 * cars as the ordinary prefix; do not filter the catalog. League departure
 * restores saved_visible. Counts 1..4096; an empty ordinary set is an atomic
 * error.
 */
enum mh_error_code	mh_menu_car_enter(struct mh_menu_car_setup_state *state,
	struct mh_menu_car_catalog *catalog, unsigned int mode,
	int special_allowed, struct mh_error *error_out);
enum mh_error_code	mh_menu_car_leave(struct mh_menu_car_catalog *catalog,
	unsigned int mode, struct mh_error *error_out);

/*
 * Ordered Up/Down/Left/Right/Escape/Enter. Only key event 1 fires. Horn count
 * 1..128; stage 0..3. MH_MENU_HORN carries a catalog index, separate from menu
 * sound events. MH_MENU_QUICK requests race launch without changing the
 * selected race mode. Unlocked count may exceed visible after League entry.
 * Failure preserves state and output.
 */
enum mh_error_code	mh_menu_car_setup_update(
	struct mh_menu_car_setup_state *state,
	const struct mh_menu_car_catalog *catalog, unsigned int mode,
	unsigned int stage, unsigned int horn_count,
	const struct mh_menu_keys *input, struct mh_menu_car_output *output,
	struct mh_error *error_out);

#endif
