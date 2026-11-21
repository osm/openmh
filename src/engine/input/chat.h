#ifndef MH_INPUT_CHAT_H
#define MH_INPUT_CHAT_H

#include "engine/core/error.h"
#include "engine/input/binding.h"
#include "engine/settings/control_layout.h"

#define MH_CHAT_HISTORY_ROWS 100
#define MH_CHAT_TEXT_BYTES 256
#define MH_CHAT_SEND_BYTES 274
#define MH_CHAT_NAME_BYTES 17
#define MH_CHAT_SHORTCUTS 10
#define MH_CHAT_SHORTCUT_BYTES 128
/* Shared race/lobby editing limits; the byte limit includes NUL. */
#define MH_CHAT_EDIT_LIMIT 200
#define MH_CHAT_EDIT_WIDTH 10000UL

/*
 * Editable history, separate from notification history. The two trailing words
 * are retained; their meaning in this history is not established. Do not treat
 * them as message clocks.
 */
struct mh_chat_draft {
	unsigned char	text[MH_CHAT_TEXT_BYTES];
	unsigned long	words[2];
};

struct mh_chat_history {
	struct mh_chat_draft	rows[MH_CHAT_HISTORY_ROWS];
	unsigned int		recall;
};

enum mh_chat_history_action {
	MH_CHAT_STORE,
	MH_CHAT_NEWER,
	MH_CHAT_OLDER,
	MH_CHAT_RESET_RECALL
};

struct mh_chat {
	struct mh_chat_history	history;
	unsigned long		active, cancelled;
};

struct mh_chat_config {
	unsigned long	mode, color;

	/* Full original 16-byte player name, with native NUL. */
	char		name[MH_CHAT_NAME_BYTES];
	char		shortcuts[MH_CHAT_SHORTCUTS][MH_CHAT_SHORTCUT_BYTES];
};

struct mh_chat_send {
	/* Name, colon, space, then the draft. */
	char		text[MH_CHAT_SEND_BYTES];
	unsigned long	color;
	int		network;
};

struct mh_chat_output {
	unsigned int		count;

	/* State on entry, including exit frame. */
	int			block_controls, skip_menu;
	struct mh_chat_send	sends[MH_CHAT_SHORTCUTS];
};

/*
 * STORE archives even an empty draft and clears only its first byte, retaining
 * metadata and bytes past NUL.
 */
enum mh_error_code	mh_chat_history_update(struct mh_chat_history *history,
	enum mh_chat_history_action action, struct mh_error *error_out);

/*
 * Race input. Inactive chat opens only in network mode 4; that frame still
 * queries all ten raw digit events in order. Active chat edits first, then
 * sends on Enter and cancels on Escape (both can act). Typing uses event==1;
 * commands use !=0. Characters and FONT2 advances are supplied by the
 * platform/assets. Errors preserve both state and output. No I/O occurs here.
 */
enum mh_error_code	mh_chat_step(struct mh_chat *chat,
	const struct mh_chat_config *config,
	const struct mh_control_binding_set *bindings,
	const struct mh_binding_input *input,
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_chat_output *output,
	struct mh_error *error_out);

#endif
