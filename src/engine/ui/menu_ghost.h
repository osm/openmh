#ifndef MH_MENU_GHOST_H
#define MH_MENU_GHOST_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_demo_catalog.h"

/*
 * Focus 0=Ghostmode, 1=Demo, 2=recording list. Drawing sets mode 3/6 from focus
 * 0/1; input alone does not change the retained mode. European 3.1 adds
 * Benchmark at focus 2 and moves the list to focus 8. patched selects that
 * layout; drawing also retains its benchmark flag.
 */
enum mh_menu_ghost_focus {
	MH_MENU_GHOST_FOCUS_RACE = 0,
	MH_MENU_GHOST_FOCUS_DEMO = 1,
	MH_MENU_GHOST_FOCUS_RECORDINGS = 2,
	/* Patched menu reuses legacy list focus 2 for Benchmark. */
	MH_MENU_GHOST_FOCUS_BENCHMARK = 2,
	MH_MENU_GHOST_FOCUS_PATCHED_RECORDINGS = 8
};

struct mh_menu_ghost_state {
	unsigned int	focus, mode, count, selected, pending_delete;
	unsigned long	delete_answer;
	int		patched, benchmark;
};

#define MH_MENU_GHOST_EVENTS 16

enum mh_menu_ghost_event {
	MH_MENU_GHOST_NAME = 5,
	MH_MENU_GHOST_DELETE,
	MH_MENU_GHOST_REFRESH
};

struct mh_menu_ghost_input {
	struct mh_menu_keys	keys;
	unsigned int		remove, confirmation;
};

struct mh_menu_ghost_output {
	struct mh_menu_event	events[MH_MENU_GHOST_EVENTS];
	unsigned int		count;
};

unsigned int	mh_menu_ghost_list_focus(
	const struct mh_menu_ghost_state *state);

/*
 * Reset focus and the deletion answer, retaining the file choice. Native count
 * bound is 4096; empty catalogs require selected zero.
 */
enum mh_error_code	mh_menu_ghost_enter(struct mh_menu_ghost_state *state,
	struct mh_error *error_out);

/*
 * Left, Right, Up, Down, Delete/answer, Enter, Escape. Only event value one
 * fires. NAME copies the indicated old-catalog filename. DELETE removes that
 * file; REFRESH rebuilds the catalog, restoring the NAME choice. If
 * pending_delete becomes one, process events in order, then call finish below
 * with the refreshed count/selection and the SAME frame's input. These two pure
 * phases preserve original synchronous file-I/O ordering without putting
 * filesystem callbacks in game/UI interfaces. Errors are atomic.
 */
enum mh_error_code	mh_menu_ghost_update(struct mh_menu_ghost_state *state,
	const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *output, struct mh_error *error_out);
enum mh_error_code	mh_menu_ghost_finish_delete(
	struct mh_menu_ghost_state *state, unsigned int count,
	unsigned int selected, const struct mh_menu_ghost_input *input,
	struct mh_menu_ghost_output *output, struct mh_error *error_out);

#endif
