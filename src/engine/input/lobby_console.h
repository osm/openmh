#ifndef MH_INPUT_LOBBY_CONSOLE_H
#define MH_INPUT_LOBBY_CONSOLE_H

#include "engine/core/error.h"
#include "engine/input/chat.h"

#define MH_LOBBY_PASSWORD_BYTES 16
#define MH_LOBBY_PASSWORD_LIMIT 15

struct mh_lobby_submission {
	int		ready, statistics;
	unsigned char	text[MH_CHAT_TEXT_BYTES];
};

/*
 * Complete the first whitespace-delimited slash token, replacing the whole
 * draft. First match wins; host/client commands have priority. ASCII case
 * folding, original trailing spaces, no completion cycling. Bytes after the
 * replacement NUL are retained. Atomic on invalid input.
 */
enum mh_error_code	mh_lobby_complete(
	unsigned char text[MH_CHAT_TEXT_BYTES], int host,
	struct mh_error *error_out);

/*
 * Edit, submit/store/reset, Tab, older, newer, in that order. Supply the
 * currently bound FONT1 advances, also used by the lobby editor. A nonzero raw
 * event triggers commands; character insertion requires 1. Caller dispatches
 * the submitted command before scrolling, then handles clipboard and other
 * lobby hotkeys. Password entry bypasses this branch. No command execution or
 * I/O here. Errors preserve history and submission.
 */
enum mh_error_code	mh_lobby_console_edit(struct mh_chat_history *history,
	int host, const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_lobby_submission *submission,
	struct mh_error *error_out);

/*
 * Shared commands precede role-specific completions. The match result is true
 * even if completing an already complete command. Held Tab shows statistics
 * only when completion fails. Patched editing retains the ordinary
 * submit/history ordering.
 */
enum mh_error_code	mh_lobby_complete_patched(
	unsigned char text[MH_CHAT_TEXT_BYTES], int host, int *matched,
	struct mh_error *error_out);
enum mh_error_code	mh_lobby_console_edit_patched(
	struct mh_chat_history *history, int host, int tab_down,
	const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], struct mh_lobby_submission *submission,
	struct mh_error *error_out);

/*
 * Separate password editor, limit 15 including NUL. Enter requests connection;
 * text is retained. Caller handles admission.
 */
enum mh_error_code	mh_lobby_password_edit(
	unsigned char text[MH_LOBBY_PASSWORD_BYTES],
	const unsigned char events[MH_KEY_COUNT],
	const unsigned char characters[MH_KEY_COUNT],
	const unsigned int advance[256], int *connect,
	struct mh_error *error_out);

#endif
