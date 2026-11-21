#ifndef MH_MENU_LEAGUE_H
#define MH_MENU_LEAGUE_H

#include "engine/core/error.h"
#include "engine/game/league_catalog.h"
#include "engine/input/text_edit.h"
#include "engine/ui/menu.h"

#define MH_MENU_LEAGUE_NAME_BYTES 64
#define MH_MENU_LEAGUE_EVENTS 784

/* Setup choices differ from both page destinations and page-array indices. */
enum mh_menu_league_choice {
	MH_MENU_LEAGUE_CHOICE_CONTINUE = 0,
	MH_MENU_LEAGUE_CHOICE_STANDINGS = 1,
	MH_MENU_LEAGUE_CHOICE_LOAD = 2,
	MH_MENU_LEAGUE_CHOICE_NEW = 3,
	MH_MENU_LEAGUE_CHOICE_DELETE = 4
};

enum mh_menu_league_removal {
	MH_MENU_LEAGUE_REMOVE_IDLE = 0,
	MH_MENU_LEAGUE_REMOVE_SELECTED = 1,
	MH_MENU_LEAGUE_REMOVE_DRAFT = 2
};

/*
 * Five-choice league page and its standings/load/new/delete pages.
 * count/selected belong to the shared league catalog. list_row/delete_row are
 * retained page fields; each list input begins by copying selected. The
 * original catalog always has a league, creating a default after its last
 * deletion. The platform supplies that replacement before finishing. name
 * mirrors the draft's 64-byte display name, edited to 15 bytes.
 */
struct mh_menu_league_state {
	unsigned int	choice, count, selected, list_row, delete_row, new_row;

	unsigned int
		pending_delete; /* 0 none, 1 delete page, 2 discarded draft. */
	unsigned long	create_answer, delete_answer;
	unsigned char	name[MH_MENU_LEAGUE_NAME_BYTES];
};

enum mh_menu_league_event {
	MH_MENU_LEAGUE_SELECT = 5,
	MH_MENU_LEAGUE_REMOVE
};

struct mh_menu_league_output {
	struct mh_menu_event	events[MH_MENU_LEAGUE_EVENTS];
	unsigned int		count, cursor;
	unsigned long		cursor_width;
};

struct mh_menu_league_new_input {
	unsigned long	clock_ms;
	unsigned int	confirmation;
	unsigned char	keys[MH_KEY_COUNT], characters[MH_KEY_COUNT];
};

/*
 * Left/Right, Enter, Escape; standings Enter starts car setup only when the
 * retained parent choice is Continue (zero).
 */
enum mh_error_code	mh_menu_league_update(
	struct mh_menu_league_state *state, const struct mh_menu_input *input,
	struct mh_menu_league_output *output, struct mh_error *error_out);
enum mh_error_code	mh_menu_league_standings_update(
	const struct mh_menu_league_state *state,
	const struct mh_menu_input *input, struct mh_menu_league_output *output,
	struct mh_error *error_out);

/*
 * Up/Down then SELECT, Enter/Escape both return to league setup. SELECT occurs
 * even without input, and Escape does not undo selection.
 */
enum mh_error_code	mh_menu_league_load_update(
	struct mh_menu_league_state *state, const struct mh_menu_keys *input,
	struct mh_menu_league_output *output, struct mh_error *error_out);

/*
 * Same selection, Enter may open the original deletion question, then Escape,
 * then the retained answer. REMOVE suspends this call until finish_remove
 * receives the post-deletion count/selection. A cancelled answer (2) persists
 * until another dialog opens.
 */
enum mh_error_code	mh_menu_league_delete_update(
	struct mh_menu_league_state *state, const struct mh_menu_keys *input,
	unsigned int confirmation, struct mh_menu_league_output *output,
	struct mh_error *error_out);

/*
 * Edit row zero BEFORE Up/Down and Enter/Escape. Both exit keys ask whether to
 * keep the already created draft; Enter takes precedence. Confirmation Off
 * always keeps it. Answer 2 removes the draft. The caller draws the name before
 * input; returned cursor uses its post-edit width, even when this call changes
 * rows. Cursor period is 200 ms, duty 100 ms. Creation, departure filename
 * assignment and rendering are separate from these controls. No SDL or
 * filesystem operations here.
 */
enum mh_error_code	mh_menu_league_new_update(
	struct mh_menu_league_state *state,
	const struct mh_menu_league_new_input *input,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_menu_league_output *output, struct mh_error *error_out);
enum mh_error_code	mh_menu_league_finish_remove(
	struct mh_menu_league_state *state, unsigned int count,
	unsigned int selected, struct mh_menu_league_output *output,
	struct mh_error *error_out);

/*
 * Every error leaves state/output unchanged. Catalogs are bounded to 4096;
 * modal input must block these callbacks until the shared dialog closes.
 */

#endif
