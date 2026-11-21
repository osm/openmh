#ifndef MH_MENU_MULTI_H
#define MH_MENU_MULTI_H

#include "engine/core/error.h"
#include "engine/input/text_edit.h"
#include "engine/settings/config.h"
#include "engine/ui/menu.h"
#include "interfaces/font/glyphs.h"

#define MH_MULTI_CATALOG_LIMIT 4096
#define MH_MULTI_ADDRESS_NAME_BYTES 32
#define MH_MULTI_IP_ADDRESS_BYTES 128
#define MH_MULTI_EVENTS 784

enum mh_menu_multi_choice {
	MH_MULTI_CHOICE_JOIN = 0,
	MH_MULTI_CHOICE_CREATE = 1
};

enum mh_menu_multi_create_row {
	MH_MULTI_CREATE_START = 0,
	MH_MULTI_CREATE_NAME = 1,
	MH_MULTI_CREATE_PASSWORD = 2
};

enum mh_menu_multi_address_row {
	MH_MULTI_ADDRESS_NAME = 0,
	MH_MULTI_ADDRESS_IP = 1
};

/* Cursor origins shared by input and the drawing overlay. */
enum mh_menu_multi_text_slot {
	MH_MULTI_SESSION_NAME_SLOT = 7,
	MH_MULTI_SESSION_PASSWORD_SLOT = 8,
	MH_MULTI_ADDRESS_NAME_SLOT = 15,
	MH_MULTI_IP_ADDRESS_SLOT = 16
};

enum mh_menu_multi_phase {
	MH_MULTI_CHOOSE = 0,
	MH_MULTI_EDIT = 1,
	MH_MULTI_SEARCH_NOTICE = 2,
	MH_MULTI_SEARCH = 3,
	MH_MULTI_SESSIONS = 4,
	MH_MULTI_SEARCHING = 5,
	MH_MULTI_ADD_ADDRESS = 16
};

enum mh_menu_multi_action {
	MH_MULTI_SOUND = 1,
	MH_MULTI_MAIN,
	MH_MULTI_HOST,
	MH_MULTI_JOIN,
	MH_MULTI_FIND,
	MH_MULTI_ADD,
	MH_MULTI_DELETE,
	MH_MULTI_CONFIRM_DELETE
};

struct mh_menu_multi_state {
	unsigned int phase, choice, create_row, address, address_row, session,
		delete_answer;
	char address_name[MH_MULTI_ADDRESS_NAME_BYTES],
		ip_address[MH_MULTI_IP_ADDRESS_BYTES];
};

struct mh_menu_multi_input {
	unsigned long	clock_ms;
	unsigned int	address_count, session_count, protected_count;
	unsigned char	keys[MH_KEY_COUNT], characters[MH_KEY_COUNT];
};

struct mh_menu_multi_output {
	struct mh_menu_event	events[MH_MULTI_EVENTS];
	unsigned int		count;
	unsigned int		cursor, cursor_slot, searching;
	unsigned long		cursor_width;
};

/*
 * Reset only phase/choice on entry. The caller initializes the other retained
 * fields. This controller implements the TCP/IP menu path; protocol must be 1.
 * Create offers Start, session name and password. The native address form has
 * only name/address. The original address catalog has two protected entries.
 * protected_count allows catalogs without local search to omit them; it must be
 * 0..count. Empty catalogs allow adding an address but cannot search or delete.
 *
 * Address storage and network discovery are caller-owned. Text editing precedes
 * navigation in the Create/Add forms; Create's Enter request precedes its text
 * edit. Effects preserve original request order. MAIN/HOST/JOIN do not change
 * phase; the outer page dispatcher leaves later. HOST uses final session
 * name/password, JOIN's value is the selected session, FIND/DELETE values are
 * address indices, ADD uses final address_name/ip_address.
 *
 * Searching is an input-time text overlay. FIND enters the native SEARCHING
 * phase until completed, so platform discovery need not block rendering or be
 * requested twice. Errors preserve all outputs.
 */
enum mh_error_code	mh_menu_multi_enter(struct mh_menu_multi_state *state,
	struct mh_error *error_out);
enum mh_error_code	mh_menu_multi_update(struct mh_menu_multi_state *state,
	struct mh_settings *settings, const struct mh_menu_multi_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_multi_output *output, struct mh_error *error_out);

/*
 * Discovery returns to the address list when no sessions were found, otherwise
 * to session selection. Platform clears keyboard state on completion. Native
 * safety: clamp a stale session selection to the last available row.
 */
enum mh_error_code	mh_menu_multi_search_complete(
	struct mh_menu_multi_state *state, unsigned int session_count,
	struct mh_error *error_out);

#endif
