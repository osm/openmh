#ifndef MH_INPUT_LOBBY_KEYS_H
#define MH_INPUT_LOBBY_KEYS_H

#include <stddef.h>

#include "engine/core/error.h"
#include "engine/input/chat.h"

#define MH_LOBBY_KEY_ACTIONS 8
#define MH_LOBBY_KEY_PLAYERS 13
#define MH_LOBBY_KEY_NAME_BYTES 17
#define MH_LOBBY_KEY_CATALOG_MAX 256
#define MH_LOBBY_KEY_MESSAGE_BYTES 64

enum mh_lobby_key_timer {
	MH_LOBBY_CAR_TIMER,
	MH_LOBBY_GEAR_TIMER,
	MH_LOBBY_TRACK_TIMER,
	MH_LOBBY_LESS_TIMER,
	MH_LOBBY_MORE_TIMER,
	MH_LOBBY_MODE_TIMER,
	MH_LOBBY_TIMERS
};

struct mh_lobby_key_values {
	unsigned long	gear, laps, mode;
	char	car[MH_LOBBY_KEY_NAME_BYTES], track[MH_LOBBY_KEY_NAME_BYTES];
};

struct mh_lobby_keys {
	unsigned int			host, player, count;
	unsigned long			confirm_clear, changed[MH_LOBBY_TIMERS];

	/* Lobby membership and pending return state. */
	unsigned long lobby[MH_LOBBY_KEY_PLAYERS],
		return_pending[MH_LOBBY_KEY_PLAYERS];
	struct mh_lobby_key_values	values;
	unsigned char			draft[MH_CHAT_TEXT_BYTES];
};

struct mh_lobby_key_name {
	char	text[MH_LOBBY_KEY_NAME_BYTES];
};

struct mh_lobby_key_catalog {
	const struct mh_lobby_key_name	*tracks, *cars;
	unsigned int			track_count, car_count, car_limit;
};

struct mh_lobby_key_input {
	/* MH_KEY_COUNT raw repeat events. */
	const unsigned char	*events;

	/* Optional held-key bytes (bit 7), separate from repeat events. */
	const unsigned char	*held;

	/* Borrowed bytes; may include a NUL. */
	const unsigned char	*clipboard;
	size_t			clipboard_size;

	/* Completed asynchronous paste, independent of this frame's keys. */
	int			clipboard_ready;
	long double		(*clock_ms)(void *context);
	void			*clock_context;
};

enum mh_lobby_key_action_kind {
	MH_LOBBY_KEY_PROFILE,
	MH_LOBBY_KEY_ROSTER,
	MH_LOBBY_KEY_CONFIRM,
	MH_LOBBY_KEY_WAIT,
	MH_LOBBY_KEY_START
};

struct mh_lobby_key_action {
	enum mh_lobby_key_action_kind	kind;

	/* Snapshot at this action, not frame end. */
	struct mh_lobby_key_values	values;

	/* CONFIRM/WAIT local messages, color e06820. */
	char				text[MH_LOBBY_KEY_MESSAGE_BYTES];
};

struct mh_lobby_key_output {
	unsigned int			count;
	struct mh_lobby_key_action	actions[MH_LOBBY_KEY_ACTIONS];
};

/*
 * Left Shift+Insert or left Ctrl+V. The platform may use this predicate before
 * fetching clipboard text; NULL means unavailable.
 */
int	mh_lobby_keys_paste(const unsigned char events[MH_KEY_COUNT]);

/*
 * Clipboard and F6,F7,F1,F2,F3,F8,F4,F5, in original order. F1..5/F8 require
 * host; password/connection/membership do not gate these controls.
 *
 * Clock is queried at each original boundary, at most twelve times, and must
 * return finite milliseconds in [0, 2^63). Timestamps truncate to their low 32
 * bits; deadline addition wraps, but the strict comparison uses the full
 * extended current time, even beyond the 32-bit timestamp range.
 *
 * No rendering, transport, OS clipboard access or race start occurs here.
 * Errors leave state/output untouched; clock reads cannot be rolled back.
 * Caller handles returned actions in order before the network poll, then
 * handles Escape. START is the boundary before grid/setup/phase changes; a
 * successful start leaves the screen without that poll/Escape step.
 */
enum mh_error_code	mh_lobby_keys_step(struct mh_lobby_keys *state,
	const struct mh_lobby_key_catalog *catalog,
	const struct mh_lobby_key_input *input,
	struct mh_lobby_key_output *output, struct mh_error *error_out);

/*
 * After the network poll. Escape with an empty chat draft requests exit; a
 * nonempty draft is archived/cleared and score confirmation is cancelled. The
 * recall index is retained. Password contents do not participate. No message is
 * sent; errors leave all supplied state intact.
 */
enum mh_error_code	mh_lobby_keys_escape(struct mh_chat_history *history,
	const unsigned char events[MH_KEY_COUNT], unsigned long *confirm_clear,
	unsigned long *quit, struct mh_error *error_out);

#endif
